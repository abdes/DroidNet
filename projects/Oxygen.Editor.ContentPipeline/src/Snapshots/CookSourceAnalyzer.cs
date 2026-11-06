// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Projects editor-owned source and submits complete native recipes in dependency frontiers.</summary>
internal sealed partial class CookSourceAnalyzer(
    ContentCookOperation operation,
    NativeArtifactLease artifacts,
    IEngineContentPipelineApi native,
    IContentImportManifestBuilder recipes,
    ISceneDescriptorGenerator scenes,
    IProjectCookScopeProvider scopes,
    CookProvenance? previous = null) : ICookSourceFactsProvider
{
    private readonly string root = Path.Combine(CookPublicationPaths.Operation(operation.Project.ProjectRoot.FullPath, operation.OperationId), "analysis", Guid.NewGuid().ToString("N"));

    /// <inheritdoc />
    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var reused = await this.ReadReusableFactsAsync(inputs, cancellationToken).ConfigureAwait(false);
        if (reused.Count == inputs.Count)
        {
            return new([.. reused.Values], [], []);
        }

        _ = Directory.CreateDirectory(this.root);
        var (prepared, diagnostics) = await this.PrepareSourcesAsync(
            inputs.Where(input => !reused.ContainsKey(input.AssetUri)),
            cancellationToken).ConfigureAwait(false);
        if (diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
        {
            return new([], [], diagnostics.ToImmutable());
        }

        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot.FullPath,
            this.root,
            [.. prepared.Select(static source => source.Job)])
        {
            Artifacts = artifacts,
            Mounts = ContentImportManifestBuilder.CreateMounts(operation.Project),
            MountsFile = ContentImportMountsFile.Of(operation.Project),
        };
        var report = await native.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        this.AddReportDiagnostics(report, prepared, diagnostics);

        var generatedPaths = prepared.Where(static source => source.Scene is not null)
            .Select(static source => FileSystemPath.From(source.Scene!.DescriptorPath)).ToHashSet();
        var analyzed = await this.CollectFactsAsync(reused, prepared, report, diagnostics, generatedPaths, cancellationToken).ConfigureAwait(false);
        return new(analyzed, CaptureGeneratedInputs(report, generatedPaths), diagnostics.ToImmutable());
    }

    // The descriptors native analysis read but the editor generated are captured from the analysis, not from disk.
    private static ImmutableArray<NativeCapturedInput> CaptureGeneratedInputs(NativeSourceAnalysisReport report, HashSet<FileSystemPath> generatedPaths)
    {
        var generatedInputs = ImmutableArray.CreateBuilder<NativeCapturedInput>();
        foreach (var path in generatedPaths)
        {
            var observation = report.Jobs.SelectMany(static job => job.Observations)
                .FirstOrDefault(item => FileSystemPath.From(item.Path) == path);
            if (observation?.Metadata is not { } metadata)
            {
                continue;
            }

            var digest = observation.Reads.FirstOrDefault(static read => read.Offset == 0 && (read.MaxBytes == 0))
                ?? throw new InvalidDataException("Native analysis did not retain the complete generated descriptor.");
            generatedInputs.Add(new(path.FullPath, Exists: true, metadata, new(path.FullPath, metadata.Size, digest.Sha256)));
        }

        return generatedInputs.ToImmutable();
    }

    // The scripts and assets a scene's slots use are cooked by its sidecar job, which native analysis does not see.
    private static IEnumerable<NativeLogicalDependency> ScriptReferences(SceneDescriptorGenerationResult? scene)
        => scene?.ScriptReferences.Select(static uri =>
        {
            var path = uri.VirtualPath;
            var kind = ContentCookAssetKinds.Of(path) == ContentCookAssetKind.Script ? "script" : "asset";
            return new NativeLogicalDependency(path, kind, JsonPointer: string.Empty, Required: true);
        }) ?? [];

    private async Task<(List<PreparedSource> Prepared, ImmutableArray<DiagnosticRecord>.Builder Diagnostics)> PrepareSourcesAsync(
        IEnumerable<ContentCookInput> inputs,
        CancellationToken cancellationToken)
    {
        var prepared = new List<PreparedSource>();
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var input in inputs)
        {
            var source = await this.PrepareAsync(input, cancellationToken).ConfigureAwait(false);
            prepared.Add(source);
            diagnostics.AddRange(source.Diagnostics);
            if (source.Scene is { } scene)
            {
                diagnostics.AddRange(scene.Diagnostics);
            }
        }

        return (prepared, diagnostics);
    }

    // A job's own diagnostics and an analysis that did not complete.
    private void AddReportDiagnostics(NativeSourceAnalysisReport report, List<PreparedSource> prepared, ImmutableArray<DiagnosticRecord>.Builder diagnostics)
    {
        foreach (var job in report.Jobs)
        {
            var owner = prepared.FirstOrDefault(source => string.Equals(source.Job.Id, job.Id, StringComparison.Ordinal))?.Input;
            var ownerPath = owner?.AssetUri.VirtualPath;
            diagnostics.AddRange(job.Diagnostics.Select(issue => issue with { AffectedVirtualPath = ownerPath }));

            if (!job.Complete && !job.Diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
            {
                diagnostics.Add(new()
                {
                    OperationId = operation.OperationId,
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = "asset_cook.analysis_incomplete",
                    Message = "Native source analysis did not complete.",
                    AffectedPath = owner?.SourceAbsolutePath.FullPath ?? job.SourcePath,
                    AffectedVirtualPath = ownerPath,
                });
            }
        }
    }

    // The facts of each prepared source that analysis completed without an error, after the reused ones.
    private async Task<ImmutableArray<CookSourceFacts>> CollectFactsAsync(
        Dictionary<AssetPath, CookSourceFacts> reused,
        List<PreparedSource> prepared,
        NativeSourceAnalysisReport report,
        ImmutableArray<DiagnosticRecord>.Builder diagnostics,
        HashSet<FileSystemPath> generatedPaths,
        CancellationToken cancellationToken)
    {
        var analyzed = ImmutableArray.CreateBuilder<CookSourceFacts>();
        analyzed.AddRange(reused.Values);
        for (var index = 0; index < prepared.Count; index++)
        {
            var source = prepared[index];
            var facts = report.Jobs[index];
            if (!facts.Complete || diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                && string.Equals(issue.AffectedVirtualPath, source.Input.AssetUri.VirtualPath, StringComparison.Ordinal)))
            {
                continue;
            }

            var inputs = await CookSavedSourceReader.ReadNativeInputsAsync(facts, path => CookPaths.TryOf(operation.Project, path), generatedPaths, cancellationToken).ConfigureAwait(false);
            var virtualPath = source.Input.AssetUri.VirtualPath;
            diagnostics.AddRange(inputs.OmittedDiagnostics(operation.OperationId, FailureDomain.ContentPipeline, virtualPath));
            if (!inputs.Outside.IsEmpty)
            {
                diagnostics.AddRange(inputs.OutsideDiagnostics(operation.OperationId, FailureDomain.ContentPipeline, virtualPath));
                continue;
            }

            var files = inputs.Files;
            analyzed.Add(new(
                source.Input,
                facts.Outputs,
                [.. facts.References, .. ScriptReferences(source.Scene)],
                [.. source.ManagedFiles, .. files.Select(file => file.SourcePath == source.Input.SourceAbsolutePath
                    ? file with { AssetUri = source.Input.AssetUri } : file)])
            { Job = source.Job, Scene = source.Scene });
        }

        return analyzed.ToImmutable();
    }

    private async Task<PreparedSource> PrepareAsync(ContentCookInput input, CancellationToken cancellationToken, CookInputSnapshot? snapshot = null)
    {
        if (input.Kind == ContentCookAssetKind.Scene)
        {
            return await this.PrepareSceneAsync(input, snapshot, cancellationToken).ConfigureAwait(false);
        }

        var managed = ImmutableArray.CreateBuilder<CookSnapshotInput>();

        // A model cooks from its import record, which the snapshot captures with the model's files.
        if (input.Kind == ContentCookAssetKind.ForeignSource)
        {
            var record = ModelRecordLookup.Find(operation.Project, input.AssetUri);
            var bytes = await CookSavedSourceReader.ReadAsync(record.RecordPath, cancellationToken).ConfigureAwait(false);
            _ = ModelImportSettings.Parse(bytes, record.Identity);
            managed.Add(new(AssetUri: null, FileSystemPath.From(record.RecordPath), CookPaths.Of(operation.Project, FileSystemPath.From(record.RecordPath)), Convert.ToHexString(SHA256.HashData(bytes))));
        }

        var textureDiagnostics = input.Kind == ContentCookAssetKind.Material
            ? await new MaterialTextureCheck(operation.OperationId, operation.Project).RunAsync(
                input,
                await CookSavedSourceReader.ReadAsync(input.SourceAbsolutePath.FullPath, cancellationToken).ConfigureAwait(false),
                cancellationToken).ConfigureAwait(false)
            : [];
        return new(input, recipes.BuildJob(input, []), managed.ToImmutable(), Scene: null) { Diagnostics = textureDiagnostics };
    }

    // A scene is projected to the descriptor the engine cooks.
    private async Task<PreparedSource> PrepareSceneAsync(
        ContentCookInput input,
        CookInputSnapshot? snapshot,
        CancellationToken cancellationToken)
    {
        var sourcePath = snapshot is null ? input.SourceAbsolutePath.FullPath : Path.Combine(snapshot.InputRoot, CookPaths.StoragePathOf(input.SourceCookPath));
        var bytes = await CookSavedSourceReader.ReadAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        ImmutableArray<CookSnapshotInput> managed = [new(input.AssetUri, input.SourceAbsolutePath, input.SourceCookPath, Convert.ToHexString(SHA256.HashData(bytes)))];
        var scene = SceneSerializer.Read(bytes, input.AssetUri);

        // A scene's name is its file name, whatever the file says; a scene a relocation renamed keeps its old name
        // inside the file until it is saved again.
        scene.Name = input.SourceAbsolutePath.Name[..^AssetFileTypes.SceneDescriptor.Length];
        var scope = new ContentCookScope(operation.Project, scopes.CreateScope(operation.Project), [input], CookTargetKind.Asset)
        {
            Artifacts = artifacts,
            PreparationRoot = this.root,
        };
        var projection = await scenes.GenerateAsync(scene, scope, cancellationToken).ConfigureAwait(false);
        projection = projection with { Dependencies = [.. projection.Dependencies.Select(this.Rebase)] };

        var projected = this.Rebase(input with
        {
            SourceAbsolutePath = FileSystemPath.From(projection.DescriptorPath),
            Role = ContentCookInputRole.GeneratedDescriptor,
            DerivedFrom = input.SourceCookPath,
        });
        return new(input, recipes.BuildJob(projected, []), managed, projection);
    }

    private ContentCookInput Rebase(ContentCookInput input)
        => input with { SourceCookPath = CookPaths.Of(operation.Project, input.SourceAbsolutePath) };

    private sealed record PreparedSource(ContentCookInput Input, ContentImportJob Job, ImmutableArray<CookSnapshotInput> ManagedFiles, SceneDescriptorGenerationResult? Scene)
    {
        public ImmutableArray<DiagnosticRecord> Diagnostics { get; init; } = [];
    }
}
