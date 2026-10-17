// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="SceneComponents"/> (scene-edits.md 3.10.1): the edit behind the inspector's "add component", with the
/// defaults and consequences of each kind.
/// </summary>
[TestClass]
public sealed class SceneComponentsTests
{
    public static IEnumerable<TestDataRow<DefaultsCase>> KindDefaults =>
    [
        Row("PointLight", SceneComponents.PointLight, static component =>
        {
            var light = component.Should().BeOfType<PointLightComponent>().Which;
            _ = light.LuminousFluxLumens.Should().Be(1600f);
            _ = light.Range.Should().Be(10f);
        }),
        Row("SpotLight", SceneComponents.SpotLight, static component =>
        {
            var light = component.Should().BeOfType<SpotLightComponent>().Which;
            _ = light.LuminousFluxLumens.Should().Be(1600f);
            _ = light.Range.Should().Be(15f);
        }),
        Row("DirectionalLight", SceneComponents.DirectionalLight, static component =>
        {
            var light = component.Should().BeOfType<DirectionalLightComponent>().Which;
            _ = light.CastsShadows.Should().BeTrue();
            _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
        }),
        Row("Scripting", SceneComponents.Scripting, static component =>
        {
            var scripting = component.Should().BeOfType<ScriptingComponent>().Which;
            var slot = scripting.Slots.Should().ContainSingle().Which;
            _ = slot.Script.Should().BeNull();
            _ = slot.Overrides.Should().BeEmpty();
        }),
        Row("Geometry", SceneComponents.Geometry, static component => _ = component.Should().BeOfType<GeometryComponent>()),
        Row("PerspectiveCamera", SceneComponents.PerspectiveCamera, static component => _ = component.Should().BeOfType<PerspectiveCamera>()),
        Row("OrthographicCamera", SceneComponents.OrthographicCamera, static component => _ = component.Should().BeOfType<OrthographicCamera>()),
        Row("LocalFogVolume", SceneComponents.LocalFogVolume, static component => _ = component.Should().BeOfType<LocalFogVolumeComponent>()),
    ];

    [TestMethod]
    public async Task AddDirectionalLight_OnIdentityRotation_SetsTheDefaultRotation()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = SceneComponents.Add(fixture.Authoring, SampleScene.B, SceneComponents.DirectionalLight);

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        _ = fixture.Node(SampleScene.B).Components.OfType<DirectionalLightComponent>().Should().ContainSingle();
        SceneAssert.SameRotation(LocalRotation(fixture, SampleScene.B), DirectionalLightComponent.DefaultLocalRotation);

        // One undo takes the light and the rotation back together.
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Node(SampleScene.B).Components.OfType<DirectionalLightComponent>().Should().BeEmpty();
        _ = LocalRotation(fixture, SampleScene.B).Should().Be(Quaternion.Identity);
    }

    [TestMethod]
    public async Task AddDirectionalLight_OnRotatedNode_KeepsTheRotation()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, MathF.PI / 2);
        var edit = SceneComponents.Add(fixture.Authoring, SampleScene.A1, SceneComponents.DirectionalLight);

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.A1).Components.OfType<DirectionalLightComponent>().Should().ContainSingle();
        SceneAssert.SameRotation(LocalRotation(fixture, SampleScene.A1), rotation);
    }

    [TestMethod]
    [DynamicData(nameof(KindDefaults))]
    public async Task Add_UsesTheKindDefaults(DefaultsCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = SceneComponents.Add(fixture.Authoring, SampleScene.B, row.Kind);

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);

        // B held only its transform, so the added component is the second.
        var components = fixture.Node(SampleScene.B).Components;
        _ = components.Should().HaveCount(2);
        row.Verify(components[1]);
    }

    [TestMethod]
    public async Task AddDirectionalLight_LeavesTheSunWithItsOwner()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = SceneComponents.Add(fixture.Authoring, SampleScene.B, SceneComponents.DirectionalLight);

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
        _ = fixture.Node(SampleScene.Sun).Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
    }

    [TestMethod]
    public async Task UnknownKind_ThrowsArgumentException()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var add = () => SceneComponents.Add(fixture.Authoring, SampleScene.B, "NoSuchKind");

        _ = add.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow(SceneComponents.Geometry, DisplayName = "Geometry")]
    [DataRow(SceneComponents.PerspectiveCamera, DisplayName = "PerspectiveCamera")]
    [DataRow(SceneComponents.OrthographicCamera, DisplayName = "OrthographicCamera")]
    [DataRow(SceneComponents.DirectionalLight, DisplayName = "DirectionalLight")]
    [DataRow(SceneComponents.PointLight, DisplayName = "PointLight")]
    [DataRow(SceneComponents.SpotLight, DisplayName = "SpotLight")]
    [DataRow(SceneComponents.LocalFogVolume, DisplayName = "LocalFogVolume")]
    [DataRow(SceneComponents.Scripting, DisplayName = "Scripting")]
    public void KindOf_CreatedComponent_IsItsKind(string kind)
    {
        var component = SceneComponents.Create(kind);

        var result = SceneComponents.KindOf(component);

        _ = result.Should().Be(kind);
    }

    [TestMethod]
    public async Task KindOf_Transform_ThrowsArgumentException()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var transform = fixture.Node(SampleScene.B).Components.OfType<TransformComponent>().Single();

        var kindOf = () => SceneComponents.KindOf(transform);

        _ = kindOf.Should().Throw<ArgumentException>();
    }

    private static TestDataRow<DefaultsCase> Row(string name, string kind, Action<GameComponent> verify)
        => new(new DefaultsCase(kind, verify)) { DisplayName = name };

    private static Quaternion LocalRotation(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<TransformComponent>().Single().LocalRotation;
}
