// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Discovers a portable source bundle from an immutable primary-file copy.</summary>
/// <param name="documents">Saved source owners and their read gates.</param>
/// <param name="coordinator">The project writer and activation lifetime.</param>
/// <param name="inspector">The existing native source-inspection capability.</param>
public sealed class SceneImportSourceDiscovery(ICookDocumentRegistry documents, IContentCookCoordinator coordinator, ISceneSourceInspector inspector)
{
    /// <summary>Inspects captured primary bytes and hashes every original dependency for coherent retention.</summary>
    /// <remarks>The enclosing retention operation owns the project operation marker through native drain.</remarks>
    /// <param name="operation">The operation holding the project writer.</param>
    /// <param name="sourcePath">The selected original primary source.</param>
    /// <param name="cancellationToken">Cancels discovery and its owned native query.</param>
    /// <param name="artifacts">Optional producer artifacts retained by the enclosing import.</param>
    /// <returns>The complete original input set and matching native metadata.</returns>
    public async Task<DiscoveredSceneSource> DiscoverAsync(ContentCookOperation operation, string sourcePath, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        coordinator.VerifyWriter(operation);
        cancellationToken.ThrowIfCancellationRequested();
        var primary = Path.GetFullPath(sourcePath);
        RequireFile(operation, primary);
        var operationRoot = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"));
        var scratch = Path.Combine(operationRoot, "discovery-" + Guid.NewGuid().ToString("N"));
        CookOutputLease.RejectReparsePoint(operationRoot);
        _ = Directory.CreateDirectory(scratch);
        var copy = Path.Combine(scratch, Path.GetFileName(primary));
        Task? retainedDrain = null;
        try
        {
            CookRunContext.Report(new(Message: "Reading source content and dependencies."));
            var primaryHash = await CookSavedSourceReader.CopyAsync(documents, primary, copy, cancellationToken).ConfigureAwait(false);
            coordinator.VerifyWriter(operation);
            var inspection = await inspector.InspectSceneSourceAsync(operation.OperationId, scratch, copy, cancellationToken, artifacts).ConfigureAwait(false);
            inspection = inspection with
            {
                Diagnostics = inspection.Diagnostics.Select(issue => issue with { OperationId = operation.OperationId, AffectedPath = primary }).ToImmutableArray(),
            };
            if (!inspection.Parsed || !inspection.Supported)
            {
                var issues = inspection.Diagnostics;
                if (!issues.Any(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal))
                {
                    issues = issues.Add(Failure(operation, primary, "The source cannot be imported with the supported content policy."));
                }

                throw new CookInputDiscoveryException(issues);
            }

            var bundle = await this.DiscoverFilesAsync(operation, primary, primaryHash, inspection, cancellationToken).ConfigureAwait(false);
            return new(bundle, inspection);
        }
        catch (ContentPipelineTerminationException failure)
        {
            retainedDrain = CleanupAfterDrainAsync(failure.DrainCompletion, scratch);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, retainedDrain);
        }
        finally
        {
            if (retainedDrain is null)
            {
                DeleteScratch(scratch);
            }
        }
    }

    private static string FindCommonRoot(string root, IEnumerable<string> paths)
    {
        foreach (var path in paths)
        {
            while (Escapes(root, path))
            {
                root = Path.GetDirectoryName(root)
                    ?? throw new InvalidDataException("Source dependencies must share a filesystem root.");
            }
        }

        return root;

        static bool Escapes(string root, string path)
        {
            var relative = Path.GetRelativePath(root, path);
            return Path.IsPathRooted(relative) || string.Equals(relative, "..", StringComparison.Ordinal)
                || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
        }
    }

    private static void RequireFile(ContentCookOperation operation, string path)
    {
        var attributes = new FileInfo(path).Attributes;
        if (attributes == (FileAttributes)(-1) || attributes.HasFlag(FileAttributes.Directory))
        {
            throw new CookInputDiscoveryException([Failure(operation, path, $"Source dependency is missing: {path}") with { Code = AssetImportDiagnosticCodes.SourceMissing }]);
        }
    }

    private static DiagnosticRecord Failure(ContentCookOperation operation, string path, string message)
        => new() { OperationId = operation.OperationId, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error, Code = AssetImportDiagnosticCodes.ImportFailed, Message = message, AffectedPath = path };

    private static async Task CleanupAfterDrainAsync(Task drain, string scratch)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            DeleteScratch(scratch);
        }
    }

    private static void DeleteScratch(string scratch)
    {
        CookOutputLease.RejectReparsePoint(scratch);
        if (Directory.Exists(scratch))
        {
            Directory.Delete(scratch, recursive: true);
        }
    }

    private async Task<ImportSourceBundle> DiscoverFilesAsync(ContentCookOperation operation, string primary, string primaryHash, SceneSourceInspectionReport inspection, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        coordinator.VerifyWriter(operation);
        var directory = Path.GetDirectoryName(primary)!;
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { primary };
        foreach (var relative in inspection.ExternalFiles)
        {
            if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative)
                || relative.Split('/', '\\').Any(static part => part is not ("." or "..")
                    && (part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || part.EndsWith('.') || part.EndsWith(' '))))
            {
                throw new CookInputDiscoveryException([Failure(operation, primary, $"Source dependency '{relative}' is not a valid relative file path.")]);
            }

            _ = paths.Add(Path.GetFullPath(Path.Combine(directory, relative)));
        }

        var root = FindCommonRoot(directory, paths);
        var inputs = ImmutableArray.CreateBuilder<CookSnapshotInput>(paths.Count);
        foreach (var path in paths.Order(StringComparer.OrdinalIgnoreCase))
        {
            cancellationToken.ThrowIfCancellationRequested();
            coordinator.VerifyWriter(operation);
            RequireFile(operation, path);
            var hash = string.Equals(path, primary, StringComparison.OrdinalIgnoreCase)
                ? primaryHash
                : await CookSavedSourceReader.HashAsync(documents, path, cancellationToken).ConfigureAwait(false);
            inputs.Add(new(AssetUri: null, path, Path.GetRelativePath(root, path).Replace('\\', '/'), hash));
        }

        return new(Path.GetRelativePath(root, primary).Replace('\\', '/'), inputs.MoveToImmutable());
    }
}
