// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Assets.Model;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class SceneDescriptorGeneratorTests
{
    [TestMethod]
    public async Task EnsureDescriptorsAsync_ShouldWriteDeterministicDescriptorsForSupportedBasicShapes()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var service = new ProceduralGeometryDescriptorService();

        var inputs = await service.EnsureDescriptorsAsync(
            scope,
            [
                new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
                new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Sphere")),
                new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Plane")),
            ],
            CancellationToken.None).ConfigureAwait(false);

        _ = inputs.Should().HaveCount(4);
        _ = inputs.Should().Contain(input => input.OutputVirtualPath == "/Content/Materials/OxygenEditor_Default.omat");
        _ = inputs.Should().Contain(input => input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = inputs.Should().Contain(input => input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Sphere.ogeo");
        _ = inputs.Should().Contain(input => input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Plane.ogeo");

        var cubeDescriptor = Path.Combine(workspace.Root, ".pipeline", "Geometry", "Engine_Generated_BasicShapes_Cube.ogeo.json");
        var sphereDescriptor = Path.Combine(workspace.Root, ".pipeline", "Geometry", "Engine_Generated_BasicShapes_Sphere.ogeo.json");
        var planeDescriptor = Path.Combine(workspace.Root, ".pipeline", "Geometry", "Engine_Generated_BasicShapes_Plane.ogeo.json");

        _ = File.Exists(cubeDescriptor).Should().BeTrue();
        _ = File.Exists(sphereDescriptor).Should().BeTrue();
        _ = File.Exists(planeDescriptor).Should().BeTrue();
        _ = File.Exists(Path.Combine(workspace.Root, ".pipeline", "Materials", "OxygenEditor_Default.omat.json")).Should().BeTrue();
        _ = (await File.ReadAllTextAsync(cubeDescriptor).ConfigureAwait(false)).Should().Contain("\"generator\": \"Cube\"");
        _ = (await File.ReadAllTextAsync(sphereDescriptor).ConfigureAwait(false)).Should().Contain("\"generator\": \"Sphere\"");
        _ = (await File.ReadAllTextAsync(planeDescriptor).ConfigureAwait(false)).Should().Contain("\"generator\": \"Plane\"");
        _ = (await File.ReadAllTextAsync(cubeDescriptor).ConfigureAwait(false)).Should()
            .Contain("\"material_ref\": \"/Content/Materials/OxygenEditor_Default.omat\"");
    }

    [TestMethod]
    public async Task GenerateAsync_ShouldEmitSceneDescriptorAndDependenciesForSupportedScene()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Cube" };
        var transform = node.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(1.0f, 2.0f, 3.0f);
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube"))),
        };
        geometry.OverrideSlots.Add(new MaterialsSlot
        {
            Material = new AssetReference<MaterialAsset>(new Uri("asset:///Content/Materials/Red.omat.json")),
        });
        _ = node.AddComponent(geometry);
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun", IsSunLight = true });
        scene.RootNodes.Add(node);

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService());
        var result = await generator.GenerateAsync(scene, scope, CancellationToken.None).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.DescriptorVirtualPath.Should().Be("/Content/Scenes/Main.oscene");
        _ = File.Exists(result.DescriptorPath).Should().BeTrue();
        _ = File.Exists(Path.Combine(
            workspace.Root,
            ".pipeline",
            "Geometry",
            "Engine_Generated_BasicShapes_Cube.ogeo.json")).Should().BeTrue();
        _ = result.Dependencies.Should().Contain(input =>
            input.AssetUri == new Uri("asset:///Content/Materials/Red.omat.json")
            && input.OutputVirtualPath == "/Content/Materials/Red.omat");
        _ = result.Dependencies.Should().Contain(input =>
            input.AssetUri == new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube"))
            && input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = result.Dependencies.Should().Contain(input =>
            input.OutputVirtualPath == "/Content/Materials/OxygenEditor_Default.omat");

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath).ConfigureAwait(false));
        var root = document.RootElement;
        _ = root.GetProperty("version").GetInt32().Should().Be(3);
        _ = root.GetProperty("name").GetString().Should().Be("Main");
        _ = root.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = root.GetProperty("renderables")[0].GetProperty("material_ref").GetString()
            .Should().Be("/Content/Materials/Red.omat");
        _ = root.GetProperty("references").GetProperty("materials")[0].GetString()
            .Should().Be("/Content/Materials/Red.omat");
        _ = root.GetProperty("cameras").GetProperty("perspective").GetArrayLength().Should().Be(1);
        _ = root.GetProperty("lights").GetProperty("directional").GetArrayLength().Should().Be(1);
    }

    [TestMethod]
    public async Task GenerateAsync_ShouldEmitAuthoredSkyAtmosphereValues()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Cube" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube"))),
        });
        scene.RootNodes.Add(node);
        scene.SetEnvironment(new SceneEnvironmentData
        {
            AtmosphereEnabled = true,
            SkyAtmosphere = new SkyAtmosphereEnvironmentData
            {
                PlanetRadiusMeters = 6_400_000.0f,
                AtmosphereHeightMeters = 90_000.0f,
                GroundAlbedoRgb = new Vector3(0.2f, 0.3f, 0.4f),
                RayleighScaleHeightMeters = 7_500.0f,
                MieScaleHeightMeters = 1_500.0f,
                MieAnisotropy = 0.75f,
                SkyLuminanceFactorRgb = new Vector3(1.1f, 1.0f, 0.9f),
                AerialPerspectiveDistanceScale = 1.25f,
                AerialScatteringStrength = 0.8f,
                AerialPerspectiveStartDepthMeters = 50.0f,
                HeightFogContribution = 0.6f,
                SunDiskEnabled = false,
            },
        });

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService());
        var result = await generator.GenerateAsync(scene, scope, CancellationToken.None).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath).ConfigureAwait(false));
        var atmosphere = document.RootElement.GetProperty("environment").GetProperty("sky_atmosphere");
        _ = atmosphere.GetProperty("planet_radius_m").GetSingle().Should().Be(6_400_000.0f);
        _ = atmosphere.GetProperty("atmosphere_height_m").GetSingle().Should().Be(90_000.0f);
        _ = atmosphere.GetProperty("ground_albedo_rgb")[0].GetSingle().Should().Be(0.2f);
        _ = atmosphere.GetProperty("rayleigh_scale_height_m").GetSingle().Should().Be(7_500.0f);
        _ = atmosphere.GetProperty("mie_scale_height_m").GetSingle().Should().Be(1_500.0f);
        _ = atmosphere.GetProperty("mie_anisotropy").GetSingle().Should().Be(0.75f);
        _ = atmosphere.GetProperty("sky_luminance_factor_rgb")[0].GetSingle().Should().Be(1.1f);
        _ = atmosphere.GetProperty("aerial_perspective_distance_scale").GetSingle().Should().Be(1.25f);
        _ = atmosphere.GetProperty("aerial_scattering_strength").GetSingle().Should().Be(0.8f);
        _ = atmosphere.GetProperty("aerial_perspective_start_depth_m").GetSingle().Should().Be(50.0f);
        _ = atmosphere.GetProperty("height_fog_contribution").GetSingle().Should().Be(0.6f);
        _ = atmosphere.GetProperty("sun_disk_enabled").GetBoolean().Should().BeFalse();
    }

    [TestMethod]
    public async Task GenerateAsync_ShouldWarnForUnsupportedManualExposureField()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Cube" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube"))),
        });
        scene.RootNodes.Add(node);
        scene.SetEnvironment(new SceneEnvironmentData
        {
            PostProcess = new PostProcessEnvironmentData
            {
                ExposureMode = ExposureMode.Manual,
                ManualExposureEv = 4.0f,
            },
        });

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService());
        var result = await generator.GenerateAsync(scene, scope, CancellationToken.None).ConfigureAwait(false);

        _ = result.Diagnostics.Should().Contain(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneUnsupportedField
            && diagnostic.Message.Contains("Environment.ManualExposureEv", StringComparison.Ordinal));
    }

    [TestMethod]
    public async Task GenerateAsync_ShouldAddAuthoredGeometryDescriptorDependency()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "AuthoredMesh" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(new Uri("asset:///Content/Geometry/Foo.ogeo.json")),
        });
        scene.RootNodes.Add(node);

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService());
        var result = await generator.GenerateAsync(scene, scope, CancellationToken.None).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.Dependencies.Should().ContainSingle(input =>
            input.Kind == ContentCookAssetKind.Geometry
            && input.AssetUri == new Uri("asset:///Content/Geometry/Foo.ogeo.json")
            && input.SourceRelativePath == "Content/Geometry/Foo.ogeo.json"
            && input.OutputVirtualPath == "/Content/Geometry/Foo.ogeo");

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath).ConfigureAwait(false));
        _ = document.RootElement.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Foo.ogeo");
    }

    [TestMethod]
    public async Task GenerateAsync_WhenSceneHasNoNodes_ShouldReturnDescriptorDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService());

        var result = await generator.GenerateAsync(scene, scope, CancellationToken.None).ConfigureAwait(false);

        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed
            && diagnostic.Severity == DiagnosticSeverity.Error);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    private static Scene CreateScene(IProject project)
        => new(project)
        {
            Name = "Main",
            Id = Guid.Parse("10000000-0000-0000-0000-000000000001"),
        };

    private static ContentCookScope CreateScope(TempWorkspace workspace)
    {
        var sceneSource = Path.Combine(workspace.Root, "Content", "Scenes", "Main.oscene.json");
        Directory.CreateDirectory(Path.GetDirectoryName(sceneSource)!);
        File.WriteAllText(sceneSource, "{}");
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
                    sceneSource,
                    "/Content/Scenes/Main.oscene",
                    ContentCookInputRole.Primary),
            ],
            CookTargetKind.CurrentScene);
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-scene-descriptor-tests", Guid.NewGuid().ToString("N"));
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
