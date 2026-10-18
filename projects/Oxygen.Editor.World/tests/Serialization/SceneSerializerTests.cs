// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;
using DistanceLodPolicy = Oxygen.Editor.World.Slots.DistanceLodPolicy;
using LevelOfDetailSlot = Oxygen.Editor.World.Slots.LevelOfDetailSlot;
using LightingSlot = Oxygen.Editor.World.Slots.LightingSlot;
using MaterialsSlot = Oxygen.Editor.World.Slots.MaterialsSlot;
using RenderingSlot = Oxygen.Editor.World.Slots.RenderingSlot;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// Verifies <see cref="SceneSerializer"/> stream contracts, null handling, unknown property resilience, and complete scene round trips.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneSerializer))]
public sealed class SceneSerializerTests
{
    [TestMethod]
    public async Task DeserializeAsync_WhenStreamContainsNullJson_ThrowsJsonException()
    {
        var stream = new MemoryStream(Encoding.UTF8.GetBytes("null"));
        await using var lifetime = stream.ConfigureAwait(false);

        var act = async () => await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = await act.Should().ThrowAsync<JsonException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task DeserializeIntoAsync_WhenStreamContainsNullJson_IsNoOp()
    {
        var scene = new Scene { Name = "Original Name" };
        var stream = new MemoryStream(Encoding.UTF8.GetBytes("null"));
        await using var lifetime = stream.ConfigureAwait(false);

        await SceneSerializer.DeserializeIntoAsync(stream, scene).ConfigureAwait(false);

        _ = scene.Name.Should().Be("Original Name");
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public async Task DeserializeAsync_WithUnknownTopLevelProperty_IgnoresUnknownAndLoadsScene()
    {
        const string json = """
            {
              "Id": "99999999-9999-9999-9999-999999999999",
              "Name": "Scene with Unknown Property",
              "ExplorerLayout": { "SelectedNode": "123", "Expanded": true },
              "RootNodes": []
            }
            """;
        var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        await using var lifetime = stream.ConfigureAwait(false);

        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = scene.Name.Should().Be("Scene with Unknown Property");
        _ = scene.Id.Should().Be(SceneId.FromValue(Guid.Parse("99999999-9999-9999-9999-999999999999")));
    }

    [TestMethod]
    public async Task SerializeAndDeserializeAsync_CompleteScene_RestoresEquivalentScene()
    {
        var scene = SceneTestSupport.CreateCompleteScene();

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        _ = restored.Name.Should().Be(scene.Name);
        _ = restored.Id.Should().Be(scene.Id);
        _ = restored.StartingCamera.Should().Be(scene.StartingCamera);
        _ = restored.StartingInputContexts.Should().Equal(scene.StartingInputContexts);
        _ = restored.References.Scripts.Should().Equal(scene.References.Scripts);
        _ = restored.References.InputActions.Should().Equal(scene.References.InputActions);
        _ = restored.References.PhysicsSidecars.Should().Equal(scene.References.PhysicsSidecars);
        _ = restored.References.ExtraAssets.Should().Equal(scene.References.ExtraAssets);
        _ = restored.Environment.Background.Should().Be(scene.Environment.Background);
        _ = restored.Environment.SkySphere.Should().Be(scene.Environment.SkySphere);
        _ = restored.Environment.SkyLight.Should().Be(scene.Environment.SkyLight);
        _ = restored.Environment.SkyAtmosphere.Should().Be(scene.Environment.SkyAtmosphere);
        _ = restored.Environment.Fog.Should().Be(scene.Environment.Fog);
        _ = restored.Environment.PostProcess.Should().Be(scene.Environment.PostProcess);
        _ = restored.RootNodes.Should().HaveCount(2);

        AssertCameraNode(restored.RootNodes[0]);
        AssertHeroNode(restored.RootNodes[1]);
    }

    private static void AssertCameraNode(SceneNode cameraNode)
    {
        _ = cameraNode.Name.Should().Be("Starting Camera Node");
        var camera = cameraNode.Components.OfType<PerspectiveCamera>().Single();
        _ = camera.FieldOfView.Should().Be(75f);
        _ = camera.ApertureF.Should().Be(4f);
        _ = camera.Iso.Should().Be(200f);
        _ = camera.ShutterRate.Should().Be(200f);
    }

    private static void AssertHeroNode(SceneNode heroNode)
    {
        _ = heroNode.Name.Should().Be("Hero Node");
        _ = heroNode.Flags.Should().HaveFlag(SceneNodeFlags.Visible | SceneNodeFlags.CastsShadows | SceneNodeFlags.ReceivesShadows);

        var sun = heroNode.Components.OfType<DirectionalLightComponent>().Single();
        _ = sun.IntensityLux.Should().Be(110_000f);
        _ = sun.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);

        var fog = heroNode.Components.OfType<LocalFogVolumeComponent>().Single();
        _ = fog.RadialFogExtinction.Should().Be(1.5f);
        _ = fog.HeightFogExtinction.Should().Be(0.8f);

        var scripting = heroNode.Components.OfType<ScriptingComponent>().Single();
        _ = scripting.Slots.Should().ContainSingle();
        _ = scripting.Slots[0].ExecutionOrder.Should().Be(10);

        var geo = heroNode.Components.OfType<GeometryComponent>().Single();
        _ = geo.Geometry!.Uri.Should().Be("asset:///Content/Meshes/Hero.geometry");
        _ = geo.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle();
        _ = geo.TargetedOverrides.Should().ContainSingle();

        _ = heroNode.Children.Should().ContainSingle();
        AssertChildNode(heroNode.Children[0]);
    }

    private static void AssertChildNode(SceneNode childNode)
    {
        _ = childNode.Name.Should().Be("Child Node");
        _ = childNode.Components.OfType<OrthographicCamera>().Should().ContainSingle();
        var pointLight = childNode.Components.OfType<PointLightComponent>().Single();
        _ = pointLight.LuminousFluxLumens.Should().Be(1200f);
        _ = pointLight.Range.Should().Be(15f);

        _ = childNode.OverrideSlots.OfType<RenderingSlot>().Single().IsVisible.OverrideValue.Should().BeFalse();
        _ = childNode.OverrideSlots.OfType<LightingSlot>().Single().ReceiveShadows.OverrideValue.Should().BeTrue();
        var lodSlot = childNode.OverrideSlots.OfType<LevelOfDetailSlot>().Single();
        _ = lodSlot.LodPolicy.OverrideValue.Should().BeOfType<DistanceLodPolicy>();

        _ = childNode.Children.Should().ContainSingle();
        var spotNode = childNode.Children[0];
        _ = spotNode.Name.Should().Be("Spotlight Node");
        var spotLight = spotNode.Components.OfType<SpotLightComponent>().Single();
        _ = spotLight.LuminousFluxLumens.Should().Be(1500f);
        _ = spotLight.Range.Should().Be(25f);
    }
}
