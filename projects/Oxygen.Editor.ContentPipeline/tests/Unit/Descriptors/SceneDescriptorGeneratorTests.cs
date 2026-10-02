// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json.Nodes;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Descriptors;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SceneDescriptorGeneratorTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>An explicit builtin material uses its native identity even without builtin geometry.</summary>
    /// <param name="materialName">The case variant of the authored builtin reference.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Default")]
    [DataRow("default")]
    public async Task AuthoredGeometryWithDefaultOverrideUsesCatalogMaterialPath(string materialName)
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene(workspace.Project);
        var node = new Oxygen.Editor.World.SceneNode(scene) { Name = "Authored mesh" };
        var geometryUri = new Uri("asset:///Content/Geometry/Imported.ogeo");
        var geometry = new Oxygen.Editor.World.GeometryComponent
        {
            Name = "Geometry",
            Geometry = new Oxygen.Managed.Assets.Model.AssetReference<Oxygen.Managed.Assets.Model.GeometryAsset>(geometryUri),
        };
        geometry.OverrideSlots.Add(new Oxygen.Editor.World.Slots.MaterialsSlot
        {
            Target = new(geometryUri, Guid.NewGuid(), new string('a', 64)),
            Material = new Oxygen.Managed.Assets.Model.AssetReference<Oxygen.Managed.Assets.Model.MaterialAsset>(AssetUris.BuildGeneratedUri("Materials/" + materialName)),
        });
        _ = node.AddComponent(geometry);
        scene.RootNodes.Add(node);
        var provider = new BuiltinCatalogFixture();
        var catalog = await provider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(provider));
        var result = await generator.GenerateAsync(scene, CreateScope(workspace), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().BeEmpty();
        var descriptor = JsonNode.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        _ = descriptor["renderables"]![0]!["material_overrides"]![0]!["material_ref"]!.GetValue<string>().Should().Be(catalog.DefaultMaterial.VirtualPath);
        _ = result.Dependencies.Should().ContainSingle(input => input.AssetUri == AssetUris.BuildGeneratedUri("Materials/Default"));
    }

    /// <summary>Preserves every native descriptor field, including thin bounds and default material parameters.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task AllBuiltinContributionsPreserveNativePayloadsAndIdentity()
    {
        using var workspace = new DescriptorWorkspace();
        var provider = new BuiltinCatalogFixture();
        var catalog = await provider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = new ProceduralGeometryDescriptorService(provider);

        var inputs = await service.EnsureDescriptorsAsync(CreateScope(workspace), catalog.AuthoringGeometries.Select(static item => item.AssetUri).ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Should().HaveCount(11);
        foreach (var definition in catalog.AuthoringGeometries)
        {
            var input = inputs.Single(input => input.AssetUri == definition.AssetUri);
            _ = input.OutputVirtualPath.Should().Be(definition.Contribution.VirtualPath);
            var written = JsonNode.Parse(await File.ReadAllTextAsync(input.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
            _ = JsonNode.DeepEquals(written, JsonNode.Parse(definition.Contribution.Descriptor.GetRawText())).Should().BeTrue(definition.Name);
        }

        var material = inputs.Single(static input => input.Kind == ContentCookAssetKind.Material);
        var materialJson = JsonNode.Parse(await File.ReadAllTextAsync(material.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = JsonNode.DeepEquals(materialJson, JsonNode.Parse(catalog.DefaultMaterial.Descriptor.GetRawText())).Should().BeTrue();
        _ = catalog.Find(AssetUris.BuildGeneratedUri("BasicShapes/IcoSphere"))!.CanonicalName.Should().Be("IcoSphere");
        _ = catalog.Find(AssetUris.BuildGeneratedUri("BasicShapes/ArrowGizmo")).Should().BeNull();
    }

    /// <summary>Does not fabricate a descriptor or default material for an unknown engine name.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task UnknownBuiltinHasNoFabricatedCookContribution()
    {
        using var workspace = new DescriptorWorkspace();
        var service = new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture());

        var inputs = await service.EnsureDescriptorsAsync(CreateScope(workspace), [AssetUris.BuildGeneratedUri("BasicShapes/Unknown")], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Should().BeEmpty();
        _ = Directory.Exists(Path.Combine(workspace.Root, ".pipeline")).Should().BeFalse();
    }

    /// <summary>Writes deterministic basic-shape descriptors and their material dependencies.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task EnsureDescriptorsAsyncShouldWriteDeterministicDescriptorsForSupportedBasicShapes()
    {
        using var workspace = new DescriptorWorkspace();
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
        using var workspace = new DescriptorWorkspace();
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
        var catalogProvider = new BuiltinCatalogFixture();
        var catalog = await catalogProvider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var slots = catalog.Find(geometry.Geometry.Uri)!.MaterialSlots;
        geometry.OverrideSlots.Add(new MaterialsSlot
        {
            Target = new(geometry.Geometry.Uri, slots.Slots[0].SlotId, slots.LayoutRevision),
            Material = new AssetReference<MaterialAsset>(new Uri("asset:///Content/Materials/Red.omat.json")),
        });
        _ = node.AddComponent(geometry);
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        _ = node.AddComponent(new DirectionalLightComponent
        {
            Name = "Sun",
            AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary,
            UsePerPixelAtmosphereTransmittance = true,
            AtmosphereDiskLuminanceScaleRgb = new Vector3(1.2f, 0.8f, 0.5f),
            ShadowBias = 0.001f,
            ShadowNormalBias = 0.04f,
            ContactShadows = true,
            CascadeCount = 3,
            CascadeDistances = new Vector4(5, 15, 40, 90),
            MaxShadowDistance = 90,
        });
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
        _ = result.Dependencies.Should().OnlyContain(input => input.Role == ContentCookInputRole.GeneratedDescriptor);
        _ = result.Dependencies.Should().Contain(input =>
            input.AssetUri == AssetUris.BuildGeneratedUri("BasicShapes/Cube")
            && input.OutputVirtualPath == "/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        _ = result.Dependencies.Should().Contain(input =>
            input.OutputVirtualPath == "/Content/Materials/OxygenEditor_Default.omat");

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        var root = document.RootElement;
        _ = root.GetProperty("version").GetInt32().Should().Be(10);
        _ = root.GetProperty("name").GetString().Should().Be("Main");
        _ = root.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Engine_Generated_BasicShapes_Cube.ogeo");
        var assignments = root.GetProperty("renderables")[0].GetProperty("material_overrides");
        _ = assignments.GetArrayLength().Should().Be(1);
        _ = assignments[0].GetProperty("slot_id").GetGuid().Should().Be(slots.Slots[0].SlotId);
        _ = assignments[0].GetProperty("material_ref").GetString()
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
        using var workspace = new DescriptorWorkspace();
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
        _ = root.GetProperty("$schema").GetString().Should().Be("oxygen.scene-descriptor.v10");
        _ = root.GetProperty("version").GetInt32().Should().Be(10);
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
        using var workspace = new DescriptorWorkspace();
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
        using var workspace = new DescriptorWorkspace();
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
        _ = atmosphere.GetProperty("rayleigh_scattering_rgb").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(5.802e-6f, 13.558e-6f, 33.1e-6f);
        _ = atmosphere.GetProperty("mie_scattering_rgb").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(3.996e-6f, 3.996e-6f, 3.996e-6f);
        _ = atmosphere.GetProperty("mie_absorption_rgb").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(4.405e-7f, 4.405e-7f, 4.405e-7f);
        _ = atmosphere.GetProperty("ozone_absorption_rgb").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(0.650e-6f, 1.881e-6f, 0.085e-6f);
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
        using var workspace = new DescriptorWorkspace();
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

    /// <summary>Emits authored geometry references for native analysis without resolving source dependencies.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncShouldPreserveAuthoredGeometryReferenceForNativeAnalysis()
    {
        using var workspace = new DescriptorWorkspace();
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
        _ = result.Dependencies.Should().BeEmpty();

        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = document.RootElement.GetProperty("renderables")[0].GetProperty("geometry_ref").GetString()
            .Should().Be("/Content/Geometry/Foo.ogeo");
    }

    /// <summary>Preserves an empty scene and its environment without synthetic nodes.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task GenerateAsyncWhenSceneHasNoNodesPreservesEnvironment()
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));

        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().NotContain(diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);
        _ = result.Dependencies.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = document.RootElement.GetProperty("nodes").GetArrayLength().Should().Be(0);
        _ = document.RootElement.GetProperty("environment").GetProperty("post_process_volume")
            .GetProperty("exposure_mode").GetInt32().Should().Be((int)ExposureMode.Auto);
    }
    /// <summary>Cooked scenes retain the captured skylight supplied by the live editor.</summary>
    /// <param name="atmosphereEnabled">Whether the captured source contains an atmosphere.</param>
    /// <returns>The asynchronous descriptor check.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task GenerateAsyncPreservesEditorCapturedSkyLight(bool atmosphereEnabled)
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene(workspace.Project);
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Root" });
        scene.SetEnvironment(new SceneEnvironmentData { AtmosphereEnabled = atmosphereEnabled });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, CreateScope(workspace), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().BeEmpty();
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        var sky = document.RootElement.GetProperty("environment").GetProperty("sky_light");
        _ = sky.GetProperty("enabled").GetBoolean().Should().BeTrue();
        _ = sky.GetProperty("source").GetInt32().Should().Be(0);
        foreach (var name in new[] { "intensity", "diffuse_intensity", "specular_intensity", "lower_hemisphere_blend_alpha", "volumetric_scattering_intensity" })
        {
            _ = sky.GetProperty(name).GetSingle().Should().Be(1.0f);
        }

        _ = sky.GetProperty("tint_rgb").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(1.0f, 1.0f, 1.0f);
        _ = sky.GetProperty("lower_hemisphere_color").EnumerateArray().Select(static item => item.GetSingle()).Should().Equal(0.02f, 0.02f, 0.03f);
        _ = sky.GetProperty("source_cubemap_angle_radians").GetSingle().Should().Be(0.0f);
        _ = sky.GetProperty("lower_hemisphere_is_solid_color").GetBoolean().Should().BeTrue();
        _ = sky.GetProperty("affect_reflections").GetBoolean().Should().BeTrue();
    }

    /// <summary>Schema bounds and coupled exposure fields reject invalid authoring states.</summary>
    [TestMethod]
    public void ValidatePostProcessRejectsMalformedValues()
    {
        PostProcessEnvironmentData[] invalid =
        [
            new() { AutoExposureBlackInfluence = float.NaN },
            new() { AutoExposureBlackInfluence = 1.1f },
            new() { AutoExposureTransitionDistanceEv = 0f },
            new() { AutoExposureTransitionDistanceEv = float.PositiveInfinity },
            new() { AutoExposureMinEv = 17f },
            new() { AutoExposureLowPercentile = 0.9f },
            new() { AutoExposureLogLuminanceRange = float.Epsilon },
            new() { AutoExposureMinLogLuminance = 20f },
            new() { AutoExposureMeteringMode = (MeteringMode)99 },
            new() { ExposureMode = (ExposureMode)99 },
            new() { ToneMapper = (ToneMappingMode)99 },
            new() { ExposureKey = 0f },
            new() { DisplayGamma = 0f },
            new() { AutoExposureCompensationCurve = default },
            new() { AutoExposureCompensationCurve = [new(2f, 1f), new(2f, 2f)] },
            new() { AutoExposureCompensationCurve = [new(0f, float.NaN)] },
            new() { AutoExposureCompensationCurve = Enumerable.Range(0, 65).Select(static i => new ExposureCompensationKeyData(i, 0f)).ToImmutableArray() },
            new() { AutoExposureMeteringMask = new Uri("mask.otex", UriKind.Relative) },
        ];
        foreach (var authored in invalid)
        {
            _ = SceneDescriptorGenerator.ValidatePostProcess(authored).Should().NotBeNullOrEmpty();
        }
    }

    /// <summary>Valid zero controls and EV values outside the old UI clamp survive unchanged.</summary>
    [TestMethod]
    public void ValidatePostProcessAcceptsSupportedBoundaryValues()
    {
        var authored = new PostProcessEnvironmentData
        {
            ManualExposureEv = 30f,
            AutoExposureTargetLuminance = 0f,
            AutoExposureSpeedUp = 0f,
            AutoExposureSpeedDown = 0f,
            AutoExposureBlackInfluence = 1f,
            AutoExposureLowPercentile = 0f,
            AutoExposureHighPercentile = 1f,
            AutoExposureMinLogLuminance = -24f,
            AutoExposureLogLuminanceRange = 56f,
        };
        _ = SceneDescriptorGenerator.ValidatePostProcess(authored).Should().BeNull();
        _ = authored.ManualExposureEv.Should().Be(30f);
    }

    /// <summary>Every authored post-process property and enum combination survives descriptor generation.</summary>
    /// <param name="exposureMode">The authored exposure mode.</param>
    /// <returns>The asynchronous descriptor mapping test.</returns>
    [TestMethod]
    [DataRow(ExposureMode.Manual)]
    [DataRow(ExposureMode.ManualCamera)]
    [DataRow(ExposureMode.Auto)]
    public async Task GenerateAsyncShouldPreserveEveryPostProcessField(ExposureMode exposureMode)
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Root" });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        foreach (var toneMapper in Enum.GetValues<ToneMappingMode>())
        {
            foreach (var metering in Enum.GetValues<MeteringMode>())
            {
                var authored = CreatePostProcessSample(exposureMode, toneMapper, metering);
                scene.SetEnvironment(new SceneEnvironmentData
                {
                    AtmosphereEnabled = false,
                    BackgroundColor = new Vector3(0.05f, 0.25f, 0.75f),
                    PostProcess = authored,
                });
                var savedScene = await RoundTripSavedSceneAsync(scene, workspace.Project).ConfigureAwait(false);
                var result = await generator.GenerateAsync(savedScene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);
                _ = result.Diagnostics.Should().BeEmpty();
                using var document = JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
                    _ = document.RootElement.GetProperty("version").GetInt32().Should().Be(10);
                var environment = document.RootElement.GetProperty("environment");
                _ = environment.GetProperty("sky_atmosphere").GetProperty("enabled").GetBoolean().Should().BeFalse();
                var post = environment.GetProperty("post_process_volume");
                foreach (var property in typeof(PostProcessEnvironmentData).GetProperties())
                {
                    var nativeName = JsonNamingPolicy.SnakeCaseLower.ConvertName(property.Name);
                    _ = post.TryGetProperty(nativeName, out var encoded).Should().BeTrue($"{property.Name} must be represented in the native descriptor");
                    switch (property.GetValue(authored))
                    {
                        case float value: _ = encoded.GetSingle().Should().Be(value, property.Name); break;
                        case bool value: _ = encoded.GetBoolean().Should().Be(value, property.Name); break;
                        case Uri value: _ = encoded.GetString().Should().Be(value.AbsolutePath[..^5], property.Name); break;
                        case ImmutableArray<ExposureCompensationKeyData> keys:
                            _ = encoded.GetArrayLength().Should().Be(keys.Length);
                            for (var index = 0; index < keys.Length; index++)
                            {
                                _ = encoded[index].GetProperty("metered_ev").GetSingle().Should().Be(keys[index].MeteredEv);
                                _ = encoded[index].GetProperty("compensation_ev").GetSingle().Should().Be(keys[index].CompensationEv);
                            }

                            break;
                        case Enum value: _ = encoded.GetInt32().Should().Be(Convert.ToInt32(value, System.Globalization.CultureInfo.InvariantCulture), property.Name); break;
                        default: Assert.Fail($"Add a descriptor check for {property.Name}."); break;
                    }
                }

                var background = environment.GetProperty("background");
                _ = background.GetProperty("enabled").GetBoolean().Should().BeTrue();
                _ = background.GetProperty("color_rgb").EnumerateArray().Select(static value => value.GetSingle()).Should().Equal(0.05f, 0.25f, 0.75f);
            }
        }
    }
    /// <summary>Rejects invalid saved Aerial Start values with a stable property navigation target.</summary>
    /// <param name="value">The saved invalid value.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(-1f)]
    [DataRow(float.NaN)]
    [DataRow(float.PositiveInfinity)]
    public async Task InvalidAerialStartNamesItsSceneAndProperty(float value)
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.SetEnvironment(scene.Environment with { SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = value } });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        var issue = result.Diagnostics.Single();
        _ = issue.Severity.Should().Be(DiagnosticSeverity.Error);
        _ = issue.AffectedEntity!.SceneId.Should().Be(scene.Id);
        _ = issue.AffectedVirtualPath.Should().Be(scope.Inputs[0].AssetUri.AbsolutePath);
        _ = issue.SuggestedAction!.Payload["PropertyPath"].Should().Be(SceneEnvironmentConstraints.AerialStartPropertyPath);
        _ = issue.SuggestedAction.Payload["AssetUri"].Should().Be(scope.Inputs[0].AssetUri.AbsoluteUri);
        _ = scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(value);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    /// <summary>The schema accepts its lower bound and the user's repaired value.</summary>
    /// <param name="value">The valid distance in meters.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(0f)]
    [DataRow(100f)]
    public async Task ValidAerialStartSurvivesDescriptorGeneration(float value)
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene(workspace.Project);
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Node" });
        scene.SetEnvironment(scene.Environment with { SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = value } });
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()));
        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().NotContain(static issue => issue.Severity == DiagnosticSeverity.Error);
        using var descriptor = System.Text.Json.JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = descriptor.RootElement.GetProperty("environment").GetProperty("sky_atmosphere").GetProperty("aerial_perspective_start_depth_m").GetSingle().Should().Be(value);
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

    private static ContentCookScope CreateScope(DescriptorWorkspace workspace)
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

    private sealed partial class DescriptorWorkspace : IDisposable
    {
        public DescriptorWorkspace()
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

    private static async Task<Scene> RoundTripSavedSceneAsync(Scene scene, IProject project)
    {
        var stream = new MemoryStream();
        await using var lifetime = stream.ConfigureAwait(false);
        var serializer = new SceneSerializer(project);
        await serializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        stream.Position = 0;
        return await serializer.DeserializeAsync(stream).ConfigureAwait(false);
    }

    private static PostProcessEnvironmentData CreatePostProcessSample(ExposureMode exposureMode, ToneMappingMode toneMapper, MeteringMode metering)
        => new()
        {
            ToneMapper = toneMapper,
            ExposureMode = exposureMode,
            ExposureEnabled = exposureMode == ExposureMode.ManualCamera,
            ExposureCompensationEv = 1.25f,
            ExposureKey = 8.75f,
            ManualExposureEv = 11.5f,
            AutoExposureMinEv = -3.25f,
            AutoExposureMaxEv = 12.75f,
            AutoExposureSpeedUp = 5.5f,
            AutoExposureSpeedDown = 1.75f,
            AutoExposureMeteringMode = metering,
            AutoExposureLowPercentile = 0.2f,
            AutoExposureHighPercentile = 0.85f,
            AutoExposureMinLogLuminance = -10.5f,
            AutoExposureLogLuminanceRange = 21.25f,
            AutoExposureTargetLuminance = 0.27f,
            AutoExposureSpotMeterRadius = 0.35f,
            AutoExposureBlackInfluence = 0.35f,
            AutoExposureTransitionDistanceEv = 2.5f,
            AutoExposureMeteringMask = new Uri("asset:///Content/Textures/Meter.otex.json"),
            AutoExposureCompensationCurve = [new(-4f, 1f), new(12f, -0.5f)],
            BloomIntensity = 0.6f,
            BloomThreshold = 2.25f,
            Saturation = 0.8f,
            Contrast = 1.4f,
            VignetteIntensity = 0.3f,
            DisplayGamma = 2.4f,
        };
}
