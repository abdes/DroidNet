// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Testably.Abstractions;

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
                AssetPath.Parse("/" + path[..^5]),
                ContentCookAssetKind.Material,
                "Content",
                path,
                FileSystemPath.From(Path.GetFullPath(path)),
                ContentCookInputRole.Primary),
            []).Id).ToArray();
        _ = ids.Distinct(StringComparer.Ordinal).Should().HaveCount(paths.Length);
    }

    /// <summary>A source that cooks one asset names its identity, without allocating a cooked destination.</summary>
    [TestMethod]
    public void BuildJobNamesTheIdentityOfASingleAssetSource()
    {
        using var workspace = new ManifestWorkspace();
        var builder = new ContentImportManifestBuilder();
        var input = new ContentCookInput(
            AssetPath.Parse("/Content/Materials/Red.omat"),
            ContentCookAssetKind.Material,
            "Content",
            "Content/Materials/Red.omat.json",
            FileSystemPath.From(Path.Combine(workspace.Root, "Content", "Materials", "Red.omat.json")),
            ContentCookInputRole.Primary);
        var material = builder.BuildJob(input, []);
        _ = material.Name.Should().Be("Red");
        _ = material.VirtualPath.Should().Be("/Content/Materials/Red.omat");
        _ = material.Layout.Should().Be(new ContentImportLayout("/Content"), "the cooker places the descriptor at its identity in its mount");
        _ = material.Output.Should().BeNull();
        var texture = builder.BuildJob(
            input with
            {
                AssetUri = AssetPath.Parse("/Content/Textures/Red.otex"),
                Kind = ContentCookAssetKind.Texture,
                SourceRelativePath = "Content/Textures/Red.otex.json",
            },
            []);
        _ = texture.VirtualPath.Should().Be("/Content/Textures/Red.otex");
        _ = texture.Output.Should().BeNull();
    }

    /// <summary>
    /// Model queries and cooking retain one complete native recipe, which names the model's identity: the virtual path
    /// of its file, under which its products are named.
    /// </summary>
    [TestMethod]
    public void BuildJobPreservesRetainedModelOptionsAndIdentity()
    {
        var input = new ContentCookInput(
            AssetPath.Parse("/Content/Vehicles/Ship/model.gltf"),
            ContentCookAssetKind.ForeignSource,
            "Content",
            "Content/Vehicles/Ship/model.gltf",
            FileSystemPath.From(Path.GetFullPath("model.gltf")),
            ContentCookInputRole.Primary);
        var settings = new ContentPipeline.Import.NativeSceneImportSettings(global::Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.CurrentSchemaVersion, global::Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.ImporterIdentity, "RetainedName", "Content/Vehicles/Ship", "model.gltf", new string('a', 64), ["model.gltf"])
        {
            MaterialSlotProvenance = global::Oxygen.Editor.ContentPipeline.Import.NativeMaterialSlotProvenance.Create(),
            BakeTransforms = true,
            NormalsPolicy = "preserve",
            TangentsPolicy = "generate",
        };
        var job = new ContentImportManifestBuilder().BuildJob(input, ["prior"], settings);
        _ = job.Type.Should().Be("gltf");
        _ = job.Name.Should().Be(settings.Name);
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content"));
        _ = job.VirtualPath.Should().Be("/Content/Vehicles/Ship/model.gltf");
        _ = job.DependsOn.Should().Equal("prior");
        _ = job.BakeTransforms.Should().BeTrue();
        _ = job.NormalsPolicy.Should().Be(settings.NormalsPolicy);
        _ = job.TangentsPolicy.Should().Be(settings.TangentsPolicy);
        _ = job.MaterialSlotProvenance.Should().BeSameAs(settings.MaterialSlotProvenance);
    }

    /// <summary>Scene exposure masks cook before the scene, at their own identity.</summary>
    [TestMethod]
    public void BuildSceneManifestIncludesMeteringMaskDependency()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace);
        var texture = new ContentCookInput(
            AssetPath.Parse("/Content/Textures/Meter.otex"),
            ContentCookAssetKind.Texture,
            "Content",
            "Content/Textures/Meter.otex.json",
            FileSystemPath.From(Path.Combine(workspace.Root, "Content", "Textures", "Meter.otex.json")),
            ContentCookInputRole.Dependency);
        var scene = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            [texture],
            Diagnostics: []);
        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, scene);
        _ = manifest.Jobs.Select(static job => job.Type).Should().Equal("texture-descriptor", "scene-descriptor");
        _ = manifest.Jobs[0].VirtualPath.Should().Be("/Content/Textures/Meter.otex");
        _ = manifest.Jobs[0].Layout.Should().Be(new ContentImportLayout("/Content"));
        _ = manifest.Jobs[1].DependsOn.Should().Equal(manifest.Jobs[0].Id);
    }

    [TestMethod]
    public void BuildSceneManifestOrdersDependenciesBeforeSceneAndUsesNativeJobTypes()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace) with
        {
            InputDependencies = System.Collections.Immutable.ImmutableDictionary<AssetPath, System.Collections.Immutable.ImmutableArray<AssetPath>>.Empty
                .Add(AssetPath.Parse("/Content/Geometry/Cube.ogeo"), [AssetPath.Parse("/Content/Materials/Red.omat")]),
        };
        var sceneDescriptorPath = Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json");
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            sceneDescriptorPath,
            "/Content/Scenes/Main.oscene",
            [
                CreateMaterialInput(workspace, "Content/Materials/Red.omat.json"),
                CreateGeometryInput(workspace, ".pipeline/Geometry/Cube.ogeo.json"),
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
    public void BuildSceneManifestNormalizesBackslashSourcePaths()
    {
        using var workspace = new ManifestWorkspace();
        var scope = CreateScope(workspace);
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            [CreateMaterialInput(workspace, @"Content\Materials\Red.omat.json")],
            Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, sceneDescriptor);

        _ = manifest.Jobs[0].Source.Should().Be("Content/Materials/Red.omat.json");
        _ = manifest.Jobs[0].Id.Should().Be("material:Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public void BuildManifestUsesDescriptorInputsForAssetCook()
    {
        using var workspace = new ManifestWorkspace();
        var projectContext = ProjectContext.FromProject(workspace.Project, new RealFileSystem());
        var scope = new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, FileSystemPath.From(workspace.Root), Path.Combine(workspace.Root, ".cooked")),
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
    public void BuildSceneManifestsCookStandaloneFolderInputsAlongsideSceneDependencies()
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
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
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
            AssetPath.Parse("/" + sourceRelativePath.Replace('\\', '/')[..^5]),
            ContentCookAssetKind.Material,
            "Content",
            sourceRelativePath,
            FileSystemPath.From(Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar))),
            ContentCookInputRole.Dependency);

    private static ContentCookInput CreateGeometryInput(ManifestWorkspace workspace, string sourceRelativePath)
        => new(
            AssetPath.Parse("/Content/Geometry/Cube.ogeo"),
            ContentCookAssetKind.Geometry,
            "Content",
            sourceRelativePath,
            FileSystemPath.From(Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar))),
            ContentCookInputRole.GeneratedDescriptor);

    private static ContentCookScope CreateScope(ManifestWorkspace workspace)
    {
        var projectContext = ProjectContext.FromProject(workspace.Project, new RealFileSystem());
        return new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, FileSystemPath.From(workspace.Root), Path.Combine(workspace.Root, ".cooked")),
            [
                new ContentCookInput(
                    AssetPath.Parse("/Content/Scenes/Main.oscene"),
                    ContentCookAssetKind.Scene,
                    "Content",
                    "Content/Scenes/Main.oscene.json",
                    FileSystemPath.From(Path.Combine(workspace.Root, "Content", "Scenes", "Main.oscene.json")),
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
            this.Project = new Project(projectInfo);
            var key = Guid.CreateVersion7();
            this.Output = new("Content", key, global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Generation(this.Root, key));
        }

        public string Root { get; }

        public Project Project { get; }

        public Publication.CookStagingRoot Output { get; }

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
