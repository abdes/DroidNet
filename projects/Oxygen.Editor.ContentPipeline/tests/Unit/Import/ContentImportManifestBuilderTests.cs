// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
internal sealed partial class ContentImportManifestBuilderTests
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
    /// A model job only schedules the model's import record, which holds its source, identity and settings: the
    /// manifest names the record and the mount, and none of what the record owns.
    /// </summary>
    [TestMethod]
    public void BuildJobSchedulesAModelByItsRecord()
    {
        var input = new ContentCookInput(
            AssetPath.Parse("/Content/Vehicles/Ship/model.gltf"),
            ContentCookAssetKind.ForeignSource,
            "Content",
            "Content/Vehicles/Ship/model.gltf",
            FileSystemPath.From(Path.GetFullPath("model.gltf")),
            ContentCookInputRole.Primary);
        var recordPath = $"imports/{Guid.CreateVersion7():D}.import.json";

        var job = new ContentImportManifestBuilder().BuildJob(input, ["prior"], recordPath);

        _ = job.Type.Should().Be("model");
        _ = job.Record.Should().Be(recordPath);
        _ = job.ManifestSource.Should().BeNull("the record names the model's source");
        _ = job.Source.Should().Be("Content/Vehicles/Ship/model.gltf", "the job stays attributed to the model's file");
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content"));
        _ = job.DependsOn.Should().Equal("prior");
        _ = job.VirtualPath.Should().BeNull("the record holds the model's identity");
        var json = JsonSerializer.Serialize(job);
        _ = json.Should().Contain("\"record\"").And.NotContain("\"source\"").And.NotContain("unit_policy");
    }

    /// <summary>A model job is scheduled by its record; the model's input has no job without one.</summary>
    [TestMethod]
    public void BuildJobRefusesAModelWithoutARecord()
    {
        var input = new ContentCookInput(
            AssetPath.Parse("/Content/Vehicles/Ship/model.gltf"),
            ContentCookAssetKind.ForeignSource,
            "Content",
            "Content/Vehicles/Ship/model.gltf",
            FileSystemPath.From(Path.GetFullPath("model.gltf")),
            ContentCookInputRole.Primary);

        var build = () => new ContentImportManifestBuilder().BuildJob(input, []);

        _ = build.Should().Throw<ArgumentException>();
    }

    /// <summary>The first import of a model discovers its files before its record exists: a format job at its identity.</summary>
    [TestMethod]
    public void BuildModelJobNamesTheIdentityBeforeARecordExists()
    {
        var job = new ContentImportManifestBuilder().BuildModelJob(
            "Content/Vehicles/Ship/model.fbx", ["prior"], "Ship", AssetPath.Parse("/Content/Vehicles/Ship/model.fbx"));

        _ = job.Type.Should().Be("fbx");
        _ = job.Name.Should().Be("Ship");
        _ = job.Source.Should().Be("Content/Vehicles/Ship/model.fbx");
        _ = job.VirtualPath.Should().Be("/Content/Vehicles/Ship/model.fbx");
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content"));
        _ = job.Record.Should().BeNull();
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
        var projectContext = workspace.Context;
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
        var projectContext = workspace.Context;
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

    private sealed partial class ManifestWorkspace : IDisposable
    {
        public ManifestWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-manifest-builder-tests", Guid.NewGuid().ToString("N"));
            this.Context = TestProject.Create(new RealFileSystem(), this.Root).Context;
            var key = Guid.CreateVersion7();
            this.Output = new("Content", key, global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Generation(this.Root, key));
        }

        public string Root { get; }

        public ProjectContext Context { get; }

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
