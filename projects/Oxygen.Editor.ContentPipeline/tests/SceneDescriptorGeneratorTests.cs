// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks the supported scene descriptor and dependency contract.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class SceneDescriptorGeneratorTests
{
    /// <summary>Gets or sets cancellation for the current test.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Writes deterministic basic-shape descriptors and their material dependencies.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task EnsureDescriptorsAsyncShouldWriteDeterministicDescriptorsForSupportedBasicShapes()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var service = new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture());

        var inputs = await service.EnsureDescriptorsAsync(
            scope,
            [
                AssetUris.BuildGeneratedUri("BasicShapes/Cube"),
                AssetUris.BuildGeneratedUri("BasicShapes/Sphere"),
                AssetUris.BuildGeneratedUri("BasicShapes/Plane"),
            ],
            this.TestContext.CancellationToken).ConfigureAwait(false);

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
        _ = (await File.ReadAllTextAsync(cubeDescriptor, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Contain("\"generator\": \"Cube\"");
        _ = (await File.ReadAllTextAsync(sphereDescriptor, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Contain("\"generator\": \"Sphere\"");
        _ = (await File.ReadAllTextAsync(planeDescriptor, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Contain("\"generator\": \"Plane\"");
        _ = (await File.ReadAllTextAsync(cubeDescriptor, this.TestContext.CancellationToken).ConfigureAwait(false)).Should()
            .Contain("\"material_ref\": \"/Content/Materials/OxygenEditor_Default.omat\"");
    }

    /// <summary>Emits the supported scene fields and geometry/material dependencies.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldEmitSceneDescriptorAndDependenciesForSupportedScene()
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
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        geometry.OverrideSlots.Add(new MaterialsSlot
        {
            Material = new AssetReference<MaterialAsset>(new Uri("asset:///Content/Materials/Red.omat.json")),
        });
        _ = node.AddComponent(geometry);
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun",
            AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary,
            UsePerPixelAtmosphereTransmittance = true, AtmosphereDiskLuminanceScaleRgb = new Vector3(1.2f, 0.8f, 0.5f),
            ShadowBias = 0.001f, ShadowNormalBias = 0.04f, ContactShadows = true,
            CascadeCount = 3, CascadeDistances = new Vector4(5, 15, 40, 90), MaxShadowDistance = 90 });
        scene.RootNodes.Add(node);

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

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
            input.AssetUri == AssetUris.BuildGeneratedUri("BasicShapes/Cube")
            && input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = result.Dependencies.Should().Contain(input =>
            input.OutputVirtualPath == "/Content/Materials/OxygenEditor_Default.omat");

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        var root = document.RootElement;
        _ = root.GetProperty("version").GetInt32().Should().Be(7);
        _ = root.GetProperty("name").GetString().Should().Be("Main");
        _ = root.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = root.GetProperty("renderables")[0].GetProperty("material_ref").GetString()
            .Should().Be("/Content/Materials/Red.omat");
        _ = root.GetProperty("references").GetProperty("materials")[0].GetString()
            .Should().Be("/Content/Materials/Red.omat");
        _ = root.GetProperty("cameras").GetProperty("perspective").GetArrayLength().Should().Be(1);
        _ = root.GetProperty("lights").GetProperty("directional").GetArrayLength().Should().Be(1);
        var light = root.GetProperty("lights").GetProperty("directional")[0];
        _ = light.GetProperty("atmosphere_light_slot").GetInt32().Should().Be(1);
        _ = light.GetProperty("use_per_pixel_atmosphere_transmittance").GetBoolean().Should().BeTrue();
        _ = light.GetProperty("atmosphere_disk_luminance_scale_rgb")[0].GetSingle().Should().Be(1.2f);
        _ = light.GetProperty("cascade_count").GetInt32().Should().Be(3);
        _ = light.GetProperty("cascade_distances")[3].GetSingle().Should().Be(90);
        _ = light.GetProperty("common").GetProperty("shadow").GetProperty("contact_shadows").GetBoolean().Should().BeTrue();
        _ = light.GetProperty("common").GetProperty("shadow").GetProperty("normal_bias").GetSingle().Should().Be(0.04f);
    }

    /// <summary>Preserves saved boolean intent as explicit local source choices at every hierarchy depth.</summary>
    /// <param name="visible">The child node's authored visibility.</param>
    /// <param name="castsShadows">The child node's authored geometry casting flag.</param>
    /// <param name="receivesShadows">The child node's authored geometry receiving flag.</param>
    /// <returns>The asynchronous descriptor mapping test.</returns>
    [TestMethod]
    [DataRow(false, false, false)]
    [DataRow(false, false, true)]
    [DataRow(false, true, false)]
    [DataRow(false, true, true)]
    [DataRow(true, false, false)]
    [DataRow(true, false, true)]
    [DataRow(true, true, false)]
    [DataRow(true, true, true)]
    public async Task GenerateAsyncShouldPreserveLocalNodeFlagsUnderOppositeParent(
        bool visible,
        bool castsShadows,
        bool receivesShadows)
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var parent = new SceneNode(scene)
        {
            Name = "Parent",
            IsVisible = !visible,
            CastsShadows = !castsShadows,
            ReceivesShadows = !receivesShadows,
        };
        var child = new SceneNode(scene)
        {
            Name = "Child",
            IsVisible = visible,
            CastsShadows = castsShadows,
            ReceivesShadows = receivesShadows,
            IsStatic = true,
            IsRayCastingSelectable = false,
            IgnoreParentTransform = true,
        };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        var savedScene = await RoundTripSavedSceneAsync(scene, workspace.Project).ConfigureAwait(false);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(savedScene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        var root = document.RootElement;
        _ = root.GetProperty("$schema").GetString().Should().Be("oxygen.scene-descriptor.v7");
        _ = root.GetProperty("version").GetInt32().Should().Be(7);
        var nodes = root.GetProperty("nodes");
        _ = nodes.GetArrayLength().Should().Be(2);
        _ = nodes[1].GetProperty("parent").GetInt32().Should().Be(0);
        var parentFlags = nodes[0].GetProperty("flags");
        _ = parentFlags.GetProperty("visible").GetString().Should().Be(visible ? "hidden" : "shown");
        _ = parentFlags.GetProperty("casts_shadows").GetString().Should().Be(castsShadows ? "off" : "on");
        _ = parentFlags.GetProperty("receives_shadows").GetString().Should().Be(receivesShadows ? "off" : "on");
        var childFlags = nodes[1].GetProperty("flags");
        _ = childFlags.GetProperty("visible").GetString().Should().Be(visible ? "shown" : "hidden");
        _ = childFlags.GetProperty("casts_shadows").GetString().Should().Be(castsShadows ? "on" : "off");
        _ = childFlags.GetProperty("receives_shadows").GetString().Should().Be(receivesShadows ? "on" : "off");
        _ = childFlags.GetProperty("static").GetBoolean().Should().BeTrue();
        _ = childFlags.GetProperty("ray_cast_selectable").GetBoolean().Should().BeFalse();
        _ = childFlags.GetProperty("ignore_parent_transform").GetBoolean().Should().BeTrue();
    }

    /// <summary>Component contribution gates retain their independent boolean wire contracts.</summary>
    /// <returns>The asynchronous descriptor mapping test.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldKeepRenderableAndLightFlagsBoolean()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Fixture", IsVisible = false, CastsShadows = true };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Light", CastsShadows = false });
        scene.RootNodes.Add(node);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        var root = document.RootElement;
        _ = root.GetProperty("nodes")[0].GetProperty("flags").GetProperty("visible").GetString().Should().Be("hidden");
        _ = root.GetProperty("nodes")[0].GetProperty("flags").GetProperty("casts_shadows").GetString().Should().Be("on");
        _ = root.GetProperty("renderables")[0].GetProperty("visible").GetBoolean().Should().BeFalse();
        _ = root.GetProperty("lights").GetProperty("directional")[0].GetProperty("common").GetProperty("casts_shadows").GetBoolean().Should().BeFalse();
    }

    /// <summary>Emits authored atmosphere values in native units.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldEmitAuthoredSkyAtmosphereValues()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Cube" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        });
        scene.RootNodes.Add(node);
        scene.SetEnvironment(new SceneEnvironmentData
        {
            AtmosphereEnabled = true,
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
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

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
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

    /// <summary>Emits manual exposure without an unsupported-field warning.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldEmitManualExposureWithoutWarnings()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var node = new SceneNode(scene) { Name = "Cube" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
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

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().NotContain(diagnostic => diagnostic.Code == ContentPipelineDiagnosticCodes.SceneUnsupportedField);
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = document.RootElement.GetProperty("environment").GetProperty("post_process_volume").GetProperty("manual_exposure_ev").GetSingle().Should().Be(4.0f);
    }

    /// <summary>Includes authored geometry descriptors in the dependency set.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldAddAuthoredGeometryDescriptorDependency()
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

        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.Dependencies.Should().ContainSingle(input =>
            input.Kind == ContentCookAssetKind.Geometry
            && input.AssetUri == new Uri("asset:///Content/Geometry/Foo.ogeo.json")
            && input.SourceRelativePath == "Content/Geometry/Foo.ogeo.json"
            && input.OutputVirtualPath == "/Content/Geometry/Foo.ogeo");

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = document.RootElement.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Foo.ogeo");
    }

    /// <summary>Rejects a scene with no nodes.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncWhenSceneHasNoNodesShouldReturnDescriptorDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));

        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed
            && diagnostic.Severity == DiagnosticSeverity.Error);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    private static Scene CreateScene(IProject project)
    {
        var scene = new Scene(project)
        {
            Name = "Main",
            Id = new Guid(0x10000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1),
        };
        scene.SetEnvironment(new SceneEnvironmentData
        {
            PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto },
        });
        return scene;
    }

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

    private sealed partial class TempWorkspace : IDisposable
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
