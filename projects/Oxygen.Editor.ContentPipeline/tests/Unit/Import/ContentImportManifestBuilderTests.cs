// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
public sealed class ContentImportManifestBuilderTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Distinct punctuation and Unicode paths remain distinct within a frontier.</summary>
    [TestMethod]
    public void BuildJobIdsDoNotCollapseSourcePaths()
    {
        var builder = new ContentImportManifestBuilder();
        var paths = new[] { "Content/a-b.omat.json", "Content/a_b.omat.json", "Content/日.omat.json", "Content/本.omat.json" };
        var ids = paths.Select(path => builder.BuildJob(
            new ContentCookInput(
            new Uri("asset:///" + path), ContentCookAssetKind.Material, "Content", path,
            Path.GetFullPath(path), "/Content/Materials/Value.omat", ContentCookInputRole.Primary), []).Id).ToArray();
        _ = ids.Distinct(StringComparer.Ordinal).Should().HaveCount(paths.Length);
    }

    /// <summary>Analysis recipes keep authored output identity without allocating a cooked destination.</summary>
    [TestMethod]
    public void BuildJobPreservesMaterialNameAndExplicitTextureIdentity()
    {
        using var workspace = new ManifestWorkspace();
        var builder = new ContentImportManifestBuilder();
        var input = new ContentCookInput(
            new Uri("asset:///Content/Materials/Red.omat.json"),
            ContentCookAssetKind.Material, "Content", "Content/Materials/Red.omat.json",
            Path.Combine(workspace.Root, "Content", "Materials", "Red.omat.json"),
            "/Content/Materials/Red.omat", ContentCookInputRole.Primary);
        var material = builder.BuildJob(input, []);
        _ = material.Name.Should().Be("Red");
        _ = material.Layout!.MaterialsDirectory.Should().Be("Materials");
        _ = material.Output.Should().BeNull();
        var texture = builder.BuildJob(
            input with
            {
                Kind = ContentCookAssetKind.Texture,
                SourceRelativePath = "Content/Textures/Red.otex.json",
                OutputVirtualPath = "/Content/Textures/Red.otex",
            }, []);
        _ = texture.VirtualPath.Should().Be("/Content/Textures/Red.otex");
        _ = texture.Output.Should().BeNull();
    }

    /// <summary>Model queries and cooking retain one complete native recipe.</summary>
    [TestMethod]
    public void BuildJobPreservesRetainedModelOptionsAndLayout()
    {
        var input = new ContentCookInput(
            new Uri("asset:///Content/SourceMedia/model.gltf"),
            ContentCookAssetKind.ForeignSource, "Content", "Content/SourceMedia/model.gltf",
            Path.GetFullPath("model.gltf"), null, ContentCookInputRole.Primary);
        var settings = new global::Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings(4, global::Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.ImporterIdentity, "Content", "RetainedName", "Content/SourceMedia", "model.gltf", new string('a', 64), ["model.gltf"], "Models/Retained")
        {
            MaterialSlotProvenance = global::Oxygen.Editor.ContentPipeline.Import.NativeMaterialSlotProvenance.Create(),
            BakeTransforms = true,
            NormalsPolicy = "preserve",
            TangentsPolicy = "generate",
        };
        var job = new ContentImportManifestBuilder().BuildJob(input, ["prior"], settings);
        _ = job.Type.Should().Be("gltf");
        _ = job.Name.Should().Be(settings.Name);
        _ = job.Layout.Should().Be(settings.CreateLayout());
        _ = job.DependsOn.Should().Equal("prior");
        _ = job.BakeTransforms.Should().BeTrue();
        _ = job.NormalsPolicy.Should().Be(settings.NormalsPolicy);
        _ = job.TangentsPolicy.Should().Be(settings.TangentsPolicy);
        _ = job.MaterialSlotProvenance.Should().BeSameAs(settings.MaterialSlotProvenance);
    }

    /// <summary>Scene exposure masks cook before the scene, in its own texture descriptor namespace.</summary>
    [TestMethod]
    public void BuildSceneManifestIncludesMeteringMaskDependency()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace);
        var texture = new ContentCookInput(
            new Uri("asset:///Content/Textures/Meter.otex.json"),
            ContentCookAssetKind.Texture, "Content", "Content/Textures/Meter.otex.json",
            Path.Combine(workspace.Root, "Content", "Textures", "Meter.otex.json"),
            "/Content/Textures/Meter.otex", ContentCookInputRole.Dependency);
        var scene = new SceneDescriptorGenerationResult(
            new Uri("asset:///Content/Scenes/Main.oscene.json"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene", [texture], Diagnostics: []);
        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, scene);
        _ = manifest.Jobs.Select(static job => job.Type).Should().Equal("texture-descriptor", "scene-descriptor");
        _ = manifest.Jobs[0].Layout!.VirtualMountRoot.Should().Be("/Content");
        _ = manifest.Jobs[0].Layout!.TextureDescriptorsDirectory.Should().BeNull();
        _ = manifest.Jobs[1].DependsOn.Should().Equal(manifest.Jobs[0].Id);
    }

    [TestMethod]
    public void BuildSceneManifest_ShouldOrderDependenciesBeforeSceneAndUseNativeJobTypes()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace) with
        {
            InputDependencies = System.Collections.Immutable.ImmutableDictionary<Uri, System.Collections.Immutable.ImmutableArray<Uri>>.Empty
                .Add(AssetUris.BuildGeneratedUri("BasicShapes/Cube"), [new("asset:///Content/Materials/Red.omat.json")]),
        };
        var sceneDescriptorPath = Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json");
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            new Uri("asset:///Content/Scenes/Main.oscene.json"),
            sceneDescriptorPath,
            "/Content/Scenes/Main.oscene",
            [
                CreateMaterialInput(workspace, "Content/Materials/Red.omat.json"),
                CreateGeometryInput(workspace, ".pipeline/Geometry/Engine_Generated_BasicShapes_Cube.ogeo.json"),
            ],
            Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, sceneDescriptor);

        _ = manifest.Output.Should().Be(workspace.Output.Path);
        _ = manifest.Layout.VirtualMountRoot.Should().Be("/Content");
        _ = manifest.Jobs.Select(static job => job.Type).Should().Equal(
            "material-descriptor",
            "geometry-descriptor",
            "scene-descriptor");
        _ = manifest.Jobs[1].DependsOn.Should().Equal(manifest.Jobs[0].Id);
        var sceneJob = manifest.Jobs[^1];
        _ = sceneJob.Source.Should().Be(".pipeline/Scenes/Main.oscene.json");
        _ = sceneJob.DependsOn.Should().Equal(manifest.Jobs[0].Id, manifest.Jobs[1].Id);
    }

    [TestMethod]
    public void BuildSceneManifest_ShouldNormalizeBackslashSourcePaths()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace);
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            new Uri("asset:///Content/Scenes/Main.oscene.json"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            [CreateMaterialInput(workspace, @"Content\Materials\Red.omat.json")],
            Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, sceneDescriptor);

        _ = manifest.Jobs[0].Source.Should().Be("Content/Materials/Red.omat.json");
        _ = manifest.Jobs[0].Id.Should().Be("material:Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public void BuildManifest_ShouldUseDescriptorInputsForAssetCook()
    {
        using var workspace = new ManifestWorkspace();
        var projectContext = ProjectContext.FromProject(workspace.Project);
        var scope = new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, workspace.Root, Path.Combine(workspace.Root, ".cooked")),
            [CreateMaterialInput(workspace, "Content/Materials/Red.omat.json")],
            CookTargetKind.Asset)
        { Output = workspace.Output };

        var manifest = new ContentImportManifestBuilder().BuildManifest(scope);

        _ = manifest.Output.Should().Be(workspace.Output.Path);
        _ = manifest.Layout.VirtualMountRoot.Should().Be("/Content");
        _ = manifest.Jobs.Should().ContainSingle();
        _ = manifest.Jobs[0].Type.Should().Be("material-descriptor");
        _ = manifest.Jobs[0].Source.Should().Be("Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public void BuildSceneManifests_ShouldCookStandaloneFolderInputsAlongsideSceneDependencies()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace) with
        {
            Inputs =
            [
                .. CreateScope(workspace).Inputs,
                CreateMaterialInput(workspace, "Content/Materials/Unreferenced.omat.json"),
            ],
        };
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            new Uri("asset:///Content/Scenes/Main.oscene.json"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            [CreateMaterialInput(workspace, "Content/Materials/Red.omat.json")],
            Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifests(scope, [sceneDescriptor]);

        _ = manifest.Jobs.Select(static job => job.Source).Should().Equal(
            "Content/Materials/Red.omat.json",
            "Content/Materials/Unreferenced.omat.json",
            ".pipeline/Scenes/Main.oscene.json");
    }

    private static ContentCookInput CreateMaterialInput(ManifestWorkspace workspace, string sourceRelativePath)
        => new(
            new Uri("asset:///" + sourceRelativePath.Replace('\\', '/')),
            ContentCookAssetKind.Material,
            "Content",
            sourceRelativePath,
            Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar)),
            "/" + sourceRelativePath.Replace('\\', '/')[..^5],
            ContentCookInputRole.Dependency);

    private static ContentCookInput CreateGeometryInput(ManifestWorkspace workspace, string sourceRelativePath)
        => new(
            AssetUris.BuildGeneratedUri("BasicShapes/Cube"),
            ContentCookAssetKind.Geometry,
            "Content",
            sourceRelativePath,
            Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar)),
            "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo",
            ContentCookInputRole.GeneratedDescriptor);

    private static ContentCookScope CreateScope(ManifestWorkspace workspace)
    {
        var projectContext = ProjectContext.FromProject(workspace.Project);
        return new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, workspace.Root, Path.Combine(workspace.Root, ".cooked")),
            [
                new ContentCookInput(
                    new Uri("asset:///Content/Scenes/Main.oscene.json"),
                    ContentCookAssetKind.Scene,
                    "Content",
                    "Content/Scenes/Main.oscene.json",
                    Path.Combine(workspace.Root, "Content", "Scenes", "Main.oscene.json"),
                    "/Content/Scenes/Main.oscene",
                    ContentCookInputRole.Primary),
            ],
            CookTargetKind.CurrentScene)
        { Output = workspace.Output };
    }

    private sealed class ManifestWorkspace : IDisposable
    {
        public ManifestWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-manifest-builder-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
            var projectInfo = new ProjectInfo("TestProject", Category.Games, this.Root)
            {
                AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            };
            this.Project = new Project(projectInfo) { Name = "TestProject" };
            var key = Guid.CreateVersion7();
            this.Output = new("Content", key, global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Generation(this.Root, key));
        }

        public string Root { get; }

        public Project Project { get; }
        public global::Oxygen.Editor.ContentPipeline.Publication.CookStagingRoot Output { get; }

        public void Dispose()
        {
            this.Output.DisposeAsync().AsTask().GetAwaiter().GetResult();
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
