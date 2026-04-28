// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Core;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class ContentImportManifestBuilderTests
{
    [TestMethod]
    public void BuildSceneManifest_ShouldOrderDependenciesBeforeSceneAndUseNativeJobTypes()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
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

        _ = manifest.Output.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
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
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var sceneDescriptor = new SceneDescriptorGenerationResult(
            new Uri("asset:///Content/Scenes/Main.oscene.json"),
            Path.Combine(workspace.Root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            [CreateMaterialInput(workspace, @"Content\Materials\Red.omat.json")],
            Diagnostics: []);

        var manifest = new ContentImportManifestBuilder().BuildSceneManifest(scope, sceneDescriptor);

        _ = manifest.Jobs[0].Source.Should().Be("Content/Materials/Red.omat.json");
        _ = manifest.Jobs[0].Id.Should().Be("material-Content-Materials-Red-omat-json");
    }

    [TestMethod]
    public void BuildManifest_ShouldUseDescriptorInputsForAssetCook()
    {
        using var workspace = new TempWorkspace();
        var projectContext = ProjectContext.FromProject(workspace.Project);
        var scope = new ContentCookScope(
            projectContext,
            new ProjectCookScope(projectContext.ProjectId, workspace.Root, Path.Combine(workspace.Root, ".cooked")),
            [CreateMaterialInput(workspace, "Content/Materials/Red.omat.json")],
            CookTargetKind.Asset);

        var manifest = new ContentImportManifestBuilder().BuildManifest(scope);

        _ = manifest.Output.Should().Be(Path.Combine(workspace.Root, ".cooked", "Content"));
        _ = manifest.Layout.VirtualMountRoot.Should().Be("/Content");
        _ = manifest.Jobs.Should().ContainSingle();
        _ = manifest.Jobs[0].Type.Should().Be("material-descriptor");
        _ = manifest.Jobs[0].Source.Should().Be("Content/Materials/Red.omat.json");
    }

    [TestMethod]
    public void BuildSceneManifests_ShouldCookStandaloneFolderInputsAlongsideSceneDependencies()
    {
        using var workspace = new TempWorkspace();
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

    private static ContentCookInput CreateMaterialInput(TempWorkspace workspace, string sourceRelativePath)
        => new(
            new Uri("asset:///Content/Materials/Red.omat.json"),
            ContentCookAssetKind.Material,
            "Content",
            sourceRelativePath,
            Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar)),
            "/Content/Materials/Red.omat",
            ContentCookInputRole.Dependency);

    private static ContentCookInput CreateGeometryInput(TempWorkspace workspace, string sourceRelativePath)
        => new(
            new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
            ContentCookAssetKind.Geometry,
            "Content",
            sourceRelativePath,
            Path.Combine(workspace.Root, sourceRelativePath.Replace('\\', Path.DirectorySeparatorChar)),
            "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo",
            ContentCookInputRole.GeneratedDescriptor);

    private static ContentCookScope CreateScope(TempWorkspace workspace)
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
            CookTargetKind.CurrentScene);
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-manifest-builder-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
            var projectInfo = new ProjectInfo("TestProject", Category.Games, this.Root)
            {
                AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            };
            this.Project = new Project(projectInfo) { Name = "TestProject" };
        }

        public string Root { get; }

        public Project Project { get; }

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
