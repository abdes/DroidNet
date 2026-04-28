// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class ContentPipelineServiceTests
{
    [TestMethod]
    public async Task CookCurrentSceneAsync_ShouldGenerateImportValidateAndInspect()
    {
        using var workspace = new TempWorkspace();
        var sceneUri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var generator = new CapturingSceneDescriptorGenerator(workspace, diagnostics: []);
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: new CookInspectionResult(
                Path.Combine(workspace.Root, ".cooked", "Content"),
                Succeeded: true,
                SourceIdentity: Guid.NewGuid(),
                Assets: [new CookedAssetEntry("/Content/Scenes/Main.oscene", ContentCookAssetKind.Scene)],
                Files: [],
                Diagnostics: []));
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(workspace.Scene, sceneUri, CancellationToken.None)
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
        _ = api.ValidatedRoot.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
        _ = api.InspectedRoot.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
    }

    [TestMethod]
    public async Task CookCurrentSceneAsync_WhenDescriptorHasError_ShouldNotImport()
    {
        using var workspace = new TempWorkspace();
        var generator = new CapturingSceneDescriptorGenerator(
            workspace,
            [
                new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(),
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                    Message = "No nodes.",
                },
            ]);
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(
                workspace.Scene,
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed);
        _ = api.ImportedManifest.Should().BeNull();
    }

    [TestMethod]
    public async Task CookAssetAsync_ShouldCookMaterialDescriptor()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var materialUri = new Uri("asset:///Content/Materials/Red.omat.json");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: new CookInspectionResult(
                Path.Combine(workspace.Root, ".cooked", "Content"),
                Succeeded: true,
                SourceIdentity: Guid.NewGuid(),
                Assets: [new CookedAssetEntry("/Content/Materials/Red.omat", ContentCookAssetKind.Material)],
                Files: [],
                Diagnostics: []));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookAssetAsync(materialUri, CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.CookedAssets.Should().ContainSingle(asset =>
            asset.SourceAssetUri == materialUri
            && asset.CookedAssetUri == new Uri("asset:///Content/Materials/Red.omat"));
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job =>
            job.Type == "material-descriptor" && job.Source == ".pipeline/Materials/Content/Materials/Red.omat.json");
        var generatedDescriptor = workspace.ReadText(".pipeline/Materials/Content/Materials/Red.omat.json");
        _ = generatedDescriptor.Should().Contain("\"base_color\"");
        _ = generatedDescriptor.Should().Contain("\"metalness\"");
        _ = generatedDescriptor.Should().Contain("\"alpha_mode\"");
        _ = generatedDescriptor.Should().NotContain("PbrMetallicRoughness");
        _ = generatedDescriptor.Should().NotContain("AlphaMode");
    }

    [TestMethod]
    public async Task CookAssetAsync_WhenSourceIsMissing_ShouldReturnDiagnosticWithoutImport()
    {
        using var workspace = new TempWorkspace();
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Missing.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == AssetImportDiagnosticCodes.SourceMissing
            && diagnostic.AffectedPath!.EndsWith(Path.Combine("Content", "Materials", "Missing.omat.json"), StringComparison.Ordinal));
        _ = api.ImportedManifest.Should().BeNull();
    }

    [TestMethod]
    public async Task CookAssetAsync_WhenNativeImportFails_ShouldReturnImportDiagnostic()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace),
            importResult: new NativeImportResult(
                Succeeded: false,
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = Guid.NewGuid(),
                        Domain = FailureDomain.AssetImport,
                        Severity = DiagnosticSeverity.Error,
                        Code = AssetImportDiagnosticCodes.ImportFailed,
                        Message = "Native import failed.",
                    },
                ]));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == AssetImportDiagnosticCodes.ImportFailed);
        _ = api.ValidatedRoot.Should().BeNull();
    }

    [TestMethod]
    public async Task CookAssetAsync_WhenValidationFails_ShouldInspectThenReturnValidationDiagnostic()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(
                Path.Combine(workspace.Root, ".cooked", "Content"),
                Succeeded: false,
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = Guid.NewGuid(),
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.ValidateFailed,
                        Message = "Index is invalid.",
                    },
                ]),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Inspection.Should().NotBeNull();
        _ = api.InspectedRoot.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
    }

    [TestMethod]
    public async Task CookAssetAsync_WhenInspectionFails_ShouldNotValidate()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: new CookInspectionResult(
                Path.Combine(workspace.Root, ".cooked", "Content"),
                Succeeded: false,
                SourceIdentity: null,
                Assets: [],
                Files: [],
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = Guid.NewGuid(),
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.InspectFailed,
                        Message = "Inspection failed.",
                    },
                ]));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.InspectFailed
            && diagnostic.OperationId == result.OperationId);
        _ = result.Inspection.Should().NotBeNull();
        _ = api.ValidatedRoot.Should().BeNull();
    }

    [TestMethod]
    public async Task CookAssetAsync_WhenManifestValidationFails_ShouldNotImport()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = new ContentPipelineService(
            new FixedProjectContextService(workspace.ProjectContext),
            new FixedCookScopeProvider(workspace.Root),
            new CapturingSceneDescriptorGenerator(workspace, diagnostics: []),
            new InvalidManifestBuilder(),
            new ContentImportManifestValidator(),
            api);

        var result = await service.CookAssetAsync(new Uri("asset:///Content/Materials/Red.omat.json"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.ManifestGenerationFailed);
        _ = api.ImportedManifest.Should().BeNull();
    }

    [TestMethod]
    public async Task CookCurrentSceneAsync_WhenDescriptorHasWarning_ShouldReturnWarningDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var generator = new CapturingSceneDescriptorGenerator(
            workspace,
            [
                new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(),
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Warning,
                    Code = ContentPipelineDiagnosticCodes.SceneUnsupportedField,
                    Message = "Unsupported scene field.",
                },
            ]);
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, generator, api);

        var result = await service.CookCurrentSceneAsync(
                workspace.Scene,
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.SucceededWithWarnings);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneUnsupportedField);
    }

    [TestMethod]
    public async Task CookFolderAsync_ShouldExpandCookableDescriptorFilesOnly()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteText("Content/Materials/Notes.txt", "not an asset");
        workspace.WriteMaterial("Content/Materials/Nested/Blue.omat.json", "Blue");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        _ = await service.CookFolderAsync(new Uri("asset:///Content/Materials"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Select(static job => job.Source).Should().Equal(
            ".pipeline/Materials/Content/Materials/Nested/Blue.omat.json",
            ".pipeline/Materials/Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public async Task CookFolderAsync_ShouldGenerateDescriptorsForSceneFiles()
    {
        using var workspace = new TempWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var generator = new CapturingSceneDescriptorGenerator(workspace, diagnostics: []);
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, generator, api);

        _ = await service.CookFolderAsync(new Uri("asset:///Content/Scenes"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = generator.Scope.Should().NotBeNull();
        _ = generator.Scope!.Inputs.Should().ContainSingle(input =>
            input.AssetUri == new Uri("asset:///Content/Scenes/Main.oscene.json"));
        _ = api.ImportedManifest.Should().NotBeNull();
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job => job.Type == "scene-descriptor");
    }

    [TestMethod]
    public async Task CookProjectAsync_ShouldCookEveryAuthoringMountWithInputs()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        var result = await service.CookProjectAsync(CancellationToken.None)
            .ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = api.ImportedManifests.Should().ContainSingle();
        _ = api.ImportedManifests[0].Jobs.Should().ContainSingle(job =>
            job.Source == ".pipeline/Materials/Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public async Task InspectCookedOutputAsync_WhenScopeIsCookedVirtualFolder_ShouldResolveSelectedCookedMount()
    {
        using var workspace = new TempWorkspace();
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        _ = await service.InspectCookedOutputAsync(new Uri("asset:///Cooked/Content"), CancellationToken.None)
            .ConfigureAwait(false);

        _ = api.InspectedRoot.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
    }

    [TestMethod]
    public async Task InspectCookedOutputAsync_WhenScopeIsNull_ShouldSkipLeadingDerivedAuthoringMount()
    {
        using var workspace = new TempWorkspace(
            [
                new ProjectMountPoint("Cooked", ".cooked"),
                new ProjectMountPoint("Content", "Content"),
            ]);
        var api = new CapturingEngineContentPipelineApi(
            validation: new CookValidationResult(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, Diagnostics: []),
            inspection: SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, diagnostics: []), api);

        _ = await service.InspectCookedOutputAsync(scopeUri: null, CancellationToken.None)
            .ConfigureAwait(false);

        _ = api.InspectedRoot.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
    }

    private static ContentPipelineService CreateService(
        TempWorkspace workspace,
        ISceneDescriptorGenerator generator,
        IEngineContentPipelineApi api)
        => new(
            new FixedProjectContextService(workspace.ProjectContext),
            new FixedCookScopeProvider(workspace.Root),
            generator,
            new ContentImportManifestBuilder(),
            new ContentImportManifestValidator(),
            api);

    private static CookInspectionResult SucceededInspection(TempWorkspace workspace)
        => new(
            Path.Combine(workspace.Root, ".cooked", "Content"),
            Succeeded: true,
            SourceIdentity: null,
            Assets: [],
            Files: [],
            Diagnostics: []);

    private sealed class CapturingSceneDescriptorGenerator(
        TempWorkspace workspace,
        IReadOnlyList<DiagnosticRecord> diagnostics) : ISceneDescriptorGenerator
    {
        public ContentCookScope? Scope { get; private set; }

        public Task<SceneDescriptorGenerationResult> GenerateAsync(
            Scene scene,
            ContentCookScope scope,
            CancellationToken cancellationToken)
        {
            this.Scope = scope;
            var descriptorPath = Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json");
            return Task.FromResult(new SceneDescriptorGenerationResult(
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                descriptorPath,
                "/Content/Scenes/Main.oscene",
                Dependencies: [],
                diagnostics));
        }
    }

    private sealed class CapturingEngineContentPipelineApi(
        CookValidationResult validation,
        CookInspectionResult inspection,
        NativeImportResult? importResult = null) : IEngineContentPipelineApi
    {
        private readonly List<ContentImportManifest> importedManifests = [];

        public ContentImportManifest? ImportedManifest { get; private set; }

        public List<ContentImportManifest> ImportedManifests => this.importedManifests;

        public string? InspectedRoot { get; private set; }

        public string? ValidatedRoot { get; private set; }

        public Task<NativeImportResult> ImportAsync(
            ContentImportManifest manifest,
            CancellationToken cancellationToken)
        {
            this.ImportedManifest = manifest;
            this.importedManifests.Add(manifest);
            return Task.FromResult(importResult ?? new NativeImportResult(Succeeded: true, Diagnostics: []));
        }

        public Task<CookInspectionResult> InspectLooseCookedRootAsync(
            string cookedRoot,
            CancellationToken cancellationToken)
        {
            this.InspectedRoot = cookedRoot;
            return Task.FromResult(inspection);
        }

        public Task<CookValidationResult> ValidateLooseCookedRootAsync(
            string cookedRoot,
            CancellationToken cancellationToken)
        {
            this.ValidatedRoot = cookedRoot;
            return Task.FromResult(validation);
        }
    }

    private sealed class InvalidManifestBuilder : IContentImportManifestBuilder
    {
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

    private sealed class FixedProjectContextService(ProjectContext context) : IProjectContextService
    {
        public ProjectContext? ActiveProject => context;

        public IObservable<ProjectContext?> ProjectChanged => throw new NotSupportedException();

        public void Activate(ProjectContext context)
        {
        }

        public void Close()
        {
        }
    }

    private sealed class FixedCookScopeProvider(string projectRoot) : IProjectCookScopeProvider
    {
        public ProjectCookScope CreateScope(ProjectContext context)
            => new(context.ProjectId, projectRoot, Path.Combine(projectRoot, ".cooked"));
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace(IReadOnlyList<ProjectMountPoint>? authoringMounts = null)
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-content-pipeline-service-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path.Combine(this.Root, "Content", "Scenes"));
            Directory.CreateDirectory(Path.Combine(this.Root, "Content", "Materials"));
            Directory.CreateDirectory(Path.Combine(this.Root, ".cooked", "Content"));
            var projectInfo = new ProjectInfo("TestProject", Category.Games, this.Root)
            {
                AuthoringMounts = authoringMounts is null ? [new ProjectMountPoint("Content", "Content")] : [.. authoringMounts],
            };
            this.Project = new Project(projectInfo) { Name = "TestProject" };
            this.ProjectContext = ProjectContext.FromProject(this.Project);
            this.Scene = new Scene(this.Project) { Name = "Main" };
        }

        public string Root { get; }

        public Project Project { get; }

        public ProjectContext ProjectContext { get; }

        public Scene Scene { get; }

        public void WriteText(string relativePath, string content)
        {
            var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, content);
        }

        public void WriteMaterial(string relativePath, string name)
            => this.WriteText(
                relativePath,
                $$"""
                {
                  "Schema": "oxygen.material.v1",
                  "Type": "PBR",
                  "Name": "{{name}}",
                  "PbrMetallicRoughness": {
                    "BaseColorFactor": [1, 0, 0, 1],
                    "MetallicFactor": 0,
                    "RoughnessFactor": 0.5
                  },
                  "AlphaMode": "OPAQUE",
                  "DoubleSided": false
                }
                """);

        public string ReadText(string relativePath)
        {
            var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
            return File.ReadAllText(path);
        }

        public async Task WriteSceneAsync(string relativePath)
        {
            var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var stream = File.Create(path);
            await new SceneSerializer(this.Project).SerializeAsync(stream, this.Scene).ConfigureAwait(false);
        }

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
