// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookOrchestrationTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Generates, imports, inspects, and validates a scene cook in order.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookCurrentSceneAsync_ShouldGenerateImportValidateAndInspect()
    {
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var sceneUri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var generator = new CapturingSceneDescriptorGenerator(diagnostics: []);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: new CookInspectionResult(workspace.Root, Succeeded: true, SourceIdentity: Guid.NewGuid(), Assets: [new CookedAssetEntry("/Content/Scenes/Main.oscene", ContentCookAssetKind.Scene)], Files: [], Diagnostics: []))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(sceneUri, CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.CookedAssets.Should().ContainSingle(asset =>
            asset.SourceAssetUri == sceneUri
            && asset.CookedAssetUri == new Uri("asset:///Content/Scenes/Main.oscene")
            && asset.Kind == ContentCookAssetKind.Scene);
        _ = generator.Scope.Should().NotBeNull();
        _ = generator.Scope!.Inputs.Should().ContainSingle(input =>
            input.SourceRelativePath == "Content/Scenes/Main.oscene.json"
            && input.OutputVirtualPath == "/Content/Scenes/Main.oscene");
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job => job.Type == "scene-descriptor");
        _ = api.ValidatedRoot.Should().Be(api.ImportedManifest.Output);
        _ = api.InspectedRoot.Should().Be(api.ImportedManifest.Output);
        _ = api.ImportedManifest.Output.Should().Contain(Path.Combine(".cooked", "generations"));
        _ = result.Validation!.CookedRoot.Should().Be(workspace.CookedRoot("Content"));
        _ = result.IsPublished.Should().BeTrue();
    }

    /// <summary>Stops before import when scene descriptor generation fails.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookCurrentSceneAsync_WhenDescriptorHasError_ShouldNotImport()
    {
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var generator = new CapturingSceneDescriptorGenerator([
                new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(),
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    Message = "No nodes.",
                },
            ]);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed);
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Projects authored material data into the native descriptor before import.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_ShouldCookMaterialDescriptor()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var materialUri = new Uri("asset:///Content/Materials/Red.omat.json");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: new CookInspectionResult(workspace.Root, Succeeded: true, SourceIdentity: Guid.NewGuid(), Assets: [new CookedAssetEntry("/Content/Materials/Red.omat", ContentCookAssetKind.Material)], Files: [], Diagnostics: []))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookAssetAsync(materialUri, CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.CookedAssets.Should().ContainSingle(asset =>
            asset.SourceAssetUri == materialUri
            && asset.CookedAssetUri == new Uri("asset:///Content/Materials/Red.omat"));
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job =>
            job.Type == "material-descriptor" && job.Source == "Content/Materials/Red.omat.json");
        var generatedDescriptor = await File.ReadAllTextAsync(Path.Combine(api.ImportedExecution!.InputRoot, "Content/Materials/Red.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = generatedDescriptor.Should().Contain("\"base_color\"");
        _ = generatedDescriptor.Should().Contain("\"metalness\"");
        _ = generatedDescriptor.Should().Contain("\"alpha_mode\"");
        _ = generatedDescriptor.Should().NotContain("PbrMetallicRoughness");
        _ = generatedDescriptor.Should().NotContain("AlphaMode");
    }

    /// <summary>Reports a missing source without starting native import.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_WhenSourceIsMissing_ShouldReturnDiagnosticWithoutImport()
    {
        using var workspace = new CookWorkspace();
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Missing.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == AssetImportDiagnosticCodes.SourceMissing
            && diagnostic.AffectedPath!.EndsWith(Path.Combine("Content", "Materials", "Missing.omat.json"), StringComparison.Ordinal));
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Preserves diagnostics from a failed native import.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_WhenNativeImportFails_ShouldReturnImportDiagnostic()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace), importResult: new NativeImportResult(Succeeded: false, Diagnostics: [new DiagnosticRecord { OperationId = Guid.NewGuid(), Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error, Code = AssetImportDiagnosticCodes.ImportFailed, Message = "Native import failed.", },]))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == AssetImportDiagnosticCodes.ImportFailed);
        _ = api.ValidatedRoot.Should().BeNull();
    }

    /// <summary>Reports validation failures after inspecting the output.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_WhenValidationFails_ShouldInspectThenReturnValidationDiagnostic()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: false, Diagnostics: [new DiagnosticRecord { OperationId = Guid.NewGuid(), Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error, Code = ContentPipelineDiagnosticCodes.ValidateFailed, Message = "Index is invalid.", },]), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Inspection.Should().NotBeNull();
        _ = api.InspectedRoot.Should().Be(api.ImportedManifest!.Output);
        _ = result.IsPublished.Should().BeFalse();
    }

    /// <summary>Skips validation when cooked output inspection fails.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_WhenInspectionFails_ShouldNotValidate()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: new CookInspectionResult(workspace.Root, Succeeded: false, SourceIdentity: null, Assets: [], Files: [], Diagnostics: [new DiagnosticRecord { OperationId = Guid.NewGuid(), Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error, Code = ContentPipelineDiagnosticCodes.InspectFailed, Message = "Inspection failed.", },]))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.InspectFailed
            && diagnostic.OperationId == result.OperationId);
        _ = result.Inspection.Should().NotBeNull();
        _ = api.ValidatedRoot.Should().BeNull();
    }

    /// <summary>Rejects invalid manifests before invoking the native tool.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookAssetAsync_WhenManifestValidationFails_ShouldNotImport()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = new ContentPipelineService(
            workspace.ContextService,
            workspace.CookCoordinator,
            new FixedCookScopeProvider(workspace.Root),
            new CapturingSceneDescriptorGenerator(diagnostics: []),
            new InvalidManifestBuilder(),
            new ContentImportManifestValidator(),
            api,
            workspace.Documents,
            workspace.Compatibility,
            workspace.Files,
            workspace.Publication);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.ManifestGenerationFailed);
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Retains scene descriptor warnings in a successful cook result.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookCurrentSceneAsync_WhenDescriptorHasWarning_ShouldReturnWarningDiagnostic()
    {
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var generator = new CapturingSceneDescriptorGenerator([
                new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(),
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Warning,
                    Code = ContentPipelineDiagnosticCodes.SceneUnsupportedField,
                    Message = "Unsupported scene field.",
                },
            ]);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.SucceededWithWarnings);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneUnsupportedField);
    }

    /// <summary>Includes only supported descriptors when cooking a folder.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookFolderAsync_ShouldExpandCookableDescriptorFilesOnly()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteText("Content/Materials/Notes.txt", "not an asset");
        workspace.WriteMaterial("Content/Materials/Nested/Blue.omat.json", "Blue");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        _ = await service.CookFolderAsync(new Uri("asset:///Content/Materials"), CancellationToken.None)
            .ConfigureAwait(false);
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Select(static job => job.Source).Should().Equal(
            "Content/Materials/Nested/Blue.omat.json",
            "Content/Materials/Red.omat.json");
    }

    /// <summary>Generates native scene descriptors for scene files in a folder.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookFolderAsync_ShouldGenerateDescriptorsForSceneFiles()
    {
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var generator = new CapturingSceneDescriptorGenerator(diagnostics: []);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, generator, api);

        _ = await service.CookFolderAsync(new Uri("asset:///Content/Scenes"), CancellationToken.None)
            .ConfigureAwait(false);
        _ = generator.Scope.Should().NotBeNull();
        _ = generator.Scope!.Inputs.Should().ContainSingle(input =>
            input.AssetUri == new Uri("asset:///Content/Scenes/Main.oscene.json"));
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job => job.Type == "scene-descriptor");
    }

    /// <summary>Cooks each authoring mount that contains supported inputs.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookProjectAsync_ShouldCookEveryAuthoringMountWithInputs()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var result = await service.CookProjectAsync(CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = api.ImportedManifests.Should().ContainSingle();
        _ = api.ImportedManifests[0].Jobs.Should().ContainSingle(job =>
            job.Source == "Content/Materials/Red.omat.json");
    }

    /// <summary>Resolves the selected cooked mount from a cooked virtual folder.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InspectCookedOutputAsync_WhenScopeIsCookedVirtualFolder_ShouldResolveSelectedCookedMount()
    {
        using var workspace = new CookWorkspace();
        await workspace.SeedEmptyPublicationAsync("Content", CancellationToken.None).ConfigureAwait(false);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var report = await service.InspectCookedOutputAsync(new Uri("asset:///Cooked/Content"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = report.Roots.Should().ContainSingle().Which.Inspection.CookedRoot.Should().Be(workspace.CookedRoot("Content"));
    }

    /// <summary>Skips derived mounts when choosing the default inspection root.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InspectCookedOutputAsync_WhenScopeIsNull_ShouldSkipLeadingDerivedAuthoringMount()
    {
        using var workspace = new CookWorkspace(
            [
                new ProjectMountPoint("Cooked", ".cooked"),
                new ProjectMountPoint("Content", "Content"),
            ]);
        await workspace.SeedEmptyPublicationAsync("Content", CancellationToken.None).ConfigureAwait(false);
        var api = new CapturingEngineContentPipelineApi(validation: new CookValidationResult(workspace.Root, Succeeded: true, Diagnostics: []), inspection: SucceededInspection(workspace))
        {
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(diagnostics: []), api);

        var report = await service.InspectCookedOutputAsync(scopeUri: null, CancellationToken.None)
            .ConfigureAwait(false);

        _ = report.Roots.Should().ContainSingle().Which.Inspection.CookedRoot.Should().Be(workspace.CookedRoot("Content"));
    }

    private sealed class InvalidManifestBuilder : IContentImportManifestBuilder
    {
        public ContentImportJob BuildModelJob(ContentCookInput input, IReadOnlyList<string> dependsOn, string name, ContentImportLayout layout, global::Oxygen.Editor.ContentPipeline.Import.NativeMaterialSlotProvenance provenance) => new ContentImportManifestBuilder().BuildModelJob(input, dependsOn, name, layout, provenance);
        public ContentImportJob BuildJob(ContentCookInput input, IReadOnlyList<string> dependsOn, global::Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings? modelSettings = null) => new ContentImportManifestBuilder().BuildJob(input, dependsOn, modelSettings);

        public ContentImportManifest BuildManifest(ContentCookScope scope)
            => CreateInvalid();

        public ContentImportManifest BuildSceneManifest(
            ContentCookScope scope,
            SceneDescriptorGenerationResult sceneDescriptor)
            => CreateInvalid();

        public ContentImportManifest BuildSceneManifests(
            ContentCookScope scope,
            IReadOnlyList<SceneDescriptorGenerationResult> sceneDescriptors)
            => CreateInvalid();

        private static ContentImportManifest CreateInvalid()
            => new(
                Version: 2,
                Output: string.Empty,
                Layout: new ContentImportLayout("Content"),
                Jobs: []);
    }
}
