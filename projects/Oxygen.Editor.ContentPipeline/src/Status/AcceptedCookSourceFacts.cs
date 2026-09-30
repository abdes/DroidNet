// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Reads acknowledged source state around accepted native facts, without native queries or source parsing.</summary>
internal sealed class AcceptedCookSourceFacts(ProjectContext project, CookProvenance publication, ICookDocumentRegistry documents) : ICookSourceFactsProvider
{
    private readonly Dictionary<Uri, CookProvenance.Product> products = publication.Products.ToDictionary(static product => product.SourceUri);

    /// <inheritdoc />
    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var sources = ImmutableArray.CreateBuilder<CookSourceFacts>();
        var issues = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var input in inputs)
        {
            try
            {
                sources.Add(await this.ReadSourceAsync(input, cancellationToken).ConfigureAwait(false));
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
            {
                issues.Add(new()
                {
                    OperationId = Guid.Empty, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                    Code = error is FileNotFoundException or DirectoryNotFoundException ? AssetImportDiagnosticCodes.SourceMissing : ContentPipelineDiagnosticCodes.ManifestGenerationFailed,
                    Message = error.Message, AffectedPath = input.SourceAbsolutePath, AffectedVirtualPath = input.AssetUri.AbsolutePath,
                });
            }
        }

        return new(sources.ToImmutable(), [], [], issues.ToImmutable())
        {
            BuiltinOwners = publication.BuiltinOwners,
        };
    }

    private async Task<CookSourceFacts> ReadSourceAsync(ContentCookInput input, CancellationToken cancellationToken)
    {
        var hash = await CookSavedSourceReader.HashAsync(documents, input.SourceAbsolutePath, cancellationToken, allowUnsavedDocuments: true).ConfigureAwait(false);
        var primary = new CookSnapshotInput(input.AssetUri, input.SourceAbsolutePath, input.SourceRelativePath, hash);
        if (!this.products.TryGetValue(input.AssetUri, out var prior)
            || prior.SourceInput is null || prior.SourceFiles.IsEmpty
            || !prior.SourceFiles.Any(file => string.Equals(file.RelativePath, input.SourceRelativePath, StringComparison.Ordinal)
                && string.Equals(file.DiscoveryHash, hash, StringComparison.Ordinal)))
        {
            return new(input, [], [], [primary]) { RequiresAnalysis = true };
        }

        var files = ImmutableArray.CreateBuilder<CookSnapshotInput>();
        var changedProbe = false;
        foreach (var accepted in prior.SourceFiles)
        {
            var file = accepted with
            {
                SourcePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, accepted.RelativePath)),
                NativeObservations = [],
            };
            if (file.Kind == CookSnapshotInputKind.File)
            {
                var currentHash = string.Equals(file.RelativePath, primary.RelativePath, StringComparison.Ordinal)
                    ? hash : await CookSavedSourceReader.HashAsync(documents, file.SourcePath, cancellationToken, allowUnsavedDocuments: true).ConfigureAwait(false);
                files.Add(file with { DiscoveryHash = currentHash });
            }
            else
            {
                var matches = CookSavedSourceReader.MatchesProbe(file);
                changedProbe |= !matches;
                files.Add(matches && file.Kind == CookSnapshotInputKind.Probe && file.Metadata is not null
                    ? file with { Metadata = CookSavedSourceReader.ReadMetadata(file.SourcePath) } : file);
            }
        }

        return new(input, prior.DeclaredOutputs, changedProbe ? [] : prior.NativeReferences, files.ToImmutable()) { RequiresAnalysis = changedProbe };
    }
}
