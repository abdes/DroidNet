// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class CapturingEngineContentPipelineApi(CookValidationResult validation, CookInspectionResult inspection, NativeImportResult? importResult = null) : IEngineContentPipelineApi, IBuiltinGeometryCatalogProvider
{
    private readonly CookInspectionResult inspected = inspection with
    {
        Assets = inspection.Assets.Select(static asset => asset with { DescriptorRelativePath = asset.DescriptorRelativePath ?? asset.VirtualPath.TrimStart('/') }).ToArray(),
    };

    public Func<ContentImportExecution, CancellationToken, Task>? BeforeImport { get; init; }

    public required Func<ContentSourceAnalysisExecution, CancellationToken, Task<global::Oxygen.Editor.ContentPipeline.Import.NativeSourceAnalysisReport>> SourceAnalysis { get; init; }

    public List<ContentImportExecution> Executions { get; } = [];

    public ContentImportExecution? ImportedExecution { get; private set; }

    public ContentImportManifest? ImportedManifest { get; private set; }

    public List<ContentImportManifest> ImportedManifests { get; } = [];

    public string? InspectedRoot { get; private set; }

    public string? ValidatedRoot { get; private set; }

    public Task<global::Oxygen.Editor.ContentPipeline.Import.NativeSourceAnalysisReport> AnalyzeSourcesAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken) => this.SourceAnalysis(execution, cancellationToken);

    public Task<BuiltinGeometryCatalog> GetBuiltinGeometryCatalogAsync(string projectRoot, string mountName, CancellationToken cancellationToken,
        Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts = null)
        => new BuiltinCatalogFixture().GetBuiltinGeometryCatalogAsync(projectRoot, mountName, cancellationToken, artifacts);

    public async Task<NativeImportResult> ImportAsync(
        ContentImportExecution execution,
        CancellationToken cancellationToken)
    {
        this.Executions.Add(execution);
        if (this.BeforeImport is { } beforeImport)
        {
            await beforeImport(execution, cancellationToken).ConfigureAwait(false);
        }

        this.ImportedExecution = execution;
        var manifest = execution.Manifest;
        this.ImportedManifest = manifest;
        this.ImportedManifests.Add(manifest);
        _ = Directory.CreateDirectory(manifest.Output);
        await File.WriteAllTextAsync(Path.Combine(manifest.Output, "container.index.bin"), "controlled native index", cancellationToken).ConfigureAwait(false);
        foreach (var asset in this.inspected.Assets)
        {
            var path = Path.Combine(manifest.Output, asset.DescriptorRelativePath!);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await File.WriteAllTextAsync(path, asset.VirtualPath, cancellationToken).ConfigureAwait(false);
        }

        foreach (var file in this.inspected.Files)
        {
            var path = Path.Combine(manifest.Output, file.RelativePath);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await File.WriteAllBytesAsync(path, new byte[checked((int)file.Size)], cancellationToken).ConfigureAwait(false);
        }

        Oxygen.Testing.NativeInventoryFixture.WriteIndex(
            manifest.Output,
            this.inspected.Assets,
            manifest.SourceKey ?? throw new InvalidOperationException("A candidate import requires its generation key."));
        return importResult ?? new NativeImportResult(Succeeded: true, Diagnostics: []);
    }

    public Task<global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport> ReadInventoryAsync(string root, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        this.InspectedRoot = root;
        if (!this.inspected.Succeeded)
        {
            throw new InvalidDataException("Controlled native index inspection failure.");
        }

        this.ValidatedRoot = root;
        return Task.FromResult(Oxygen.Testing.NativeInventoryFixture.Read(root, forceFailure: !validation.Succeeded));
    }

    public Task<CookInspectionResult> InspectLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken)
    {
        this.InspectedRoot = cookedRoot;
        return Task.FromResult(this.inspected with { CookedRoot = cookedRoot });
    }

    public Task<CookValidationResult> ValidateLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken)
    {
        this.ValidatedRoot = cookedRoot;
        return Task.FromResult(validation with { CookedRoot = cookedRoot });
    }
}
