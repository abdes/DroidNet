// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

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
                "/" + path,
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
            "/Content/Materials/Red.omat.json",
            FileSystemPath.From(Path.Combine(workspace.Root, "Content", "Materials", "Red.omat.json")),
            ContentCookInputRole.Primary);
        var material = builder.BuildJob(input, []);
        _ = material.Name.Should().Be("Red");
        _ = material.VirtualPath.Should().Be("/Content/Materials/Red.omat");
        _ = material.Output.Should().BeNull();
        var texture = builder.BuildJob(
            input with
            {
                AssetUri = AssetPath.Parse("/Content/Textures/Red.otex"),
                Kind = ContentCookAssetKind.Texture,
                SourceCookPath = "/Content/Textures/Red.otex.json",
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
            "/Content/Vehicles/Ship/model.gltf",
            FileSystemPath.From(Path.GetFullPath("model.gltf")),
            ContentCookInputRole.Primary);

        var job = new ContentImportManifestBuilder().BuildJob(input, ["prior"]);

        _ = job.Type.Should().Be("model");
        _ = job.VirtualPath.Should().Be("/Content/Vehicles/Ship/model.gltf", "the tool reads the record at the model's identity");
        _ = job.ManifestSource.Should().BeNull("the record names the model's source");
        _ = job.Source.Should().Be("/Content/Vehicles/Ship/model.gltf", "the job stays attributed to the model's file, by its mount path");
        _ = job.DependsOn.Should().Equal("prior");
        var json = JsonSerializer.Serialize(job);
        _ = json.Should().Contain("\"virtual_path\"").And.NotContain("\"record\"").And.NotContain("\"source\"").And.NotContain("unit_policy");
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
        _ = job.IsModel.Should().BeFalse();
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
        _ = manifest.Jobs.Select(static job => job.Type).Should().Equal(
            "material-descriptor",
            "geometry-descriptor",
            "scene-descriptor");
        _ = manifest.Jobs[1].DependsOn.Should().Equal(manifest.Jobs[0].Id);
        var sceneJob = manifest.Jobs[^1];
        _ = sceneJob.Source.Should().Be(".pipeline/Scenes/Main.oscene.json");
        _ = sceneJob.DependsOn.Should().Equal(manifest.Jobs[0].Id, manifest.Jobs[1].Id);
    }

    /// <summary>The manifest declares the project's source mounts and names no absolute path.</summary>
    [TestMethod]
    public void BuildManifestDeclaresTheProjectMountsWithoutAbsolutePaths()
    {
        using var workspace = new ManifestWorkspace();
        var projectContext = workspace.Context;
        var scope = new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, FileSystemPath.From(workspace.Root)),
            [CreateMaterialInput(workspace, "Content/Materials/Red.omat.json")],
            CookTargetKind.Asset)
        { Output = workspace.Output };

        var manifest = new ContentImportManifestBuilder().BuildManifest(scope);

        _ = manifest.Mounts.Should().NotBeNull();
        _ = manifest.Mounts!.Select(static mount => mount.Name).Should().Equal(projectContext.DeclaredMounts.Where(static mount => mount.Kind == MountKind.Source).Select(static mount => mount.Name));
        _ = manifest.Mounts.Should().Contain(new ContentImportMount("Content", "Content"));
        var text = JsonSerializer.Serialize(manifest);
        using var json = JsonDocument.Parse(text);
        _ = json.RootElement.GetProperty("mounts").GetArrayLength().Should().Be(manifest.Mounts.Count);
        _ = json.RootElement.TryGetProperty("output", out _).Should().BeFalse();
        _ = json.RootElement.TryGetProperty("layout", out _).Should().BeFalse("a cooked root holds one mount, the one its identities name");
        _ = text.Should().NotContain(JsonSerializer.Serialize(workspace.Root)[1..^1], "every manifest path is relative");
    }

    /// <summary>Each job reads its source by its mount path, whatever separators its file path has.</summary>
    [TestMethod]
    public void BuildSceneManifestNamesEachSourceByItsMountPath()
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

        _ = manifest.Jobs[0].Source.Should().Be("/Content/Materials/Red.omat.json");
        _ = manifest.Jobs[0].Id.Should().Be("material:/Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public void BuildManifestUsesDescriptorInputsForAssetCook()
    {
        using var workspace = new ManifestWorkspace();
        var projectContext = workspace.Context;
        var scope = new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, FileSystemPath.From(workspace.Root)),
            [CreateMaterialInput(workspace, "Content/Materials/Red.omat.json")],
            CookTargetKind.Asset)
        { Output = workspace.Output };

        var manifest = new ContentImportManifestBuilder().BuildManifest(scope);

        _ = manifest.Output.Should().Be(workspace.Output.Path);
        _ = manifest.Jobs.Should().ContainSingle();
        _ = manifest.Jobs[0].Type.Should().Be("material-descriptor");
        _ = manifest.Jobs[0].Source.Should().Be("/Content/Materials/Red.omat.json");
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
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Unreferenced.omat.json",
            ".pipeline/Scenes/Main.oscene.json");
    }

    [TestMethod]
    public void BuildSceneManifestsMakeASceneWaitForTheScenesItInstances()
    {
        using var workspace = new ManifestWorkspace();
        var main = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var village = AssetPath.Parse("/Content/Scenes/Village.oscene");
        var scope = CreateScope(workspace) with
        {
            InputDependencies = System.Collections.Immutable.ImmutableDictionary<AssetPath, System.Collections.Immutable.ImmutableArray<AssetPath>>.Empty
                .Add(main, [village]),
        };
        SceneDescriptorGenerationResult Descriptor(AssetPath scene, string name)
            => new(scene, Path.Combine(workspace.Root, ".pipeline", "Scenes", name + ".oscene.json"), scene.VirtualPath, [], Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifests(scope, [Descriptor(main, "Main"), Descriptor(village, "Village")]);

        var mainJob = manifest.Jobs.Single(job => string.Equals(job.Source, ".pipeline/Scenes/Main.oscene.json", StringComparison.Ordinal));
        var villageJob = manifest.Jobs.Single(job => string.Equals(job.Source, ".pipeline/Scenes/Village.oscene.json", StringComparison.Ordinal));
        _ = mainJob.DependsOn.Should().Equal(villageJob.Id);
        _ = villageJob.DependsOn.Should().BeEmpty();
    }

    // The input of a file, named by its path below the project root, with the cook path the cook gives it.
    private static ContentCookInput CreateMaterialInput(ManifestWorkspace workspace, string projectPath)
        => CreateInput(workspace, projectPath, AssetPath.Parse("/" + projectPath.Replace('\\', '/')[..^5]), ContentCookAssetKind.Material, ContentCookInputRole.Dependency);

    private static ContentCookInput CreateGeometryInput(ManifestWorkspace workspace, string projectPath)
        => CreateInput(workspace, projectPath, AssetPath.Parse("/Content/Geometry/Cube.ogeo"), ContentCookAssetKind.Geometry, ContentCookInputRole.GeneratedDescriptor);

    private static ContentCookInput CreateInput(ManifestWorkspace workspace, string projectPath, AssetPath identity, ContentCookAssetKind kind, ContentCookInputRole role)
    {
        var file = FileSystemPath.From(Path.Combine(workspace.Root, projectPath.Replace('\\', Path.DirectorySeparatorChar)));
        return new(identity, kind, "Content", CookPaths.Of(workspace.Context, file), file, role);
    }

    private static ContentCookScope CreateScope(ManifestWorkspace workspace)
    {
        var projectContext = workspace.Context;
        return new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, FileSystemPath.From(workspace.Root)),
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
            this.Output = new("Content", key, CookPublicationPaths.Generation(this.Root, key));
        }

        public string Root { get; }

        public ProjectContext Context { get; }

        public CookStagingRoot Output { get; }

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
