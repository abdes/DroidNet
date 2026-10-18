// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Interop;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>Checks the wire table against the scene's properties and the editor module's field ids.</summary>
[TestClass]
public sealed class SceneWireTests
{
    private static readonly string[] ProjectedKinds =
    [
        SceneProperties.TransformKind,
        SceneProperties.PerspectiveCameraKind,
        SceneProperties.OrthographicCameraKind,
        SceneProperties.DirectionalLightKind,
        SceneProperties.PointLightKind,
        SceneProperties.SpotLightKind,
        SceneProperties.LocalFogVolumeKind,
        SceneProperties.NodeRenderingKind,
    ];

    [TestMethod]
    public void EveryEntry_NamesARealProperty()
        => _ = SceneWire.Properties.Should().OnlyContain(property => SceneProperties.RulesOf(property.ComponentKind) != null
            && SceneProperties.RulesOf(property.ComponentKind)!.Descriptors.ContainsKey(property));

    [TestMethod]
    public void EveryPropertyOfAProjectedKind_HasAnEntry()
    {
        var properties = ProjectedKinds.SelectMany(static kind => SceneProperties.RulesOf(kind)!.Descriptors.Keys);

        _ = properties.Where(static property => !SceneWire.TryGet(property, out _)).Should().BeEmpty();
    }

    [TestMethod]
    public void EveryEntry_HasOneFieldPerEncodedValue()
    {
        foreach (var property in SceneWire.Properties)
        {
            var rules = SceneProperties.RulesOf(property.ComponentKind)!;
            var descriptor = rules.Descriptors[property];
            _ = SceneWire.TryGet(property, out var mapping).Should().BeTrue();
            var value = descriptor.ValueType.IsValueType ? Activator.CreateInstance(descriptor.ValueType) : null;
            _ = mapping.EncodeValue(value).Should().HaveCount(mapping.Fields.Length, "{0} encodes into its fields", property);
        }
    }

    [TestMethod]
    public void FieldOfView_TravelsInRadians()
    {
        _ = SceneWire.TryGet(SceneProperties.PerspectiveCamera.FieldOfViewDegreesDescriptor.Id, out var mapping).Should().BeTrue();

        _ = mapping.Component.Should().Be(EngineComponentId.PerspectiveCamera);
        _ = mapping.Fields.Should().Equal((ushort)PerspectiveCameraField.FieldOfViewYRadians);
        _ = mapping.EncodeValue(90f).Should().ContainSingle().Which.Should().BeApproximately(MathF.PI / 2, 0.00001f);
    }

    [TestMethod]
    public void ComponentPresence_CoversEveryEngineComponent()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        node.Components.Add(new PerspectiveCamera { Name = "Camera" });
        node.Components.Add(new SpotLightComponent { Name = "Spot" });

        _ = SceneWire.HasComponent(node, EngineComponentId.Transform).Should().BeTrue();
        _ = SceneWire.HasComponent(node, EngineComponentId.Node).Should().BeTrue();
        _ = SceneWire.HasComponent(node, EngineComponentId.PerspectiveCamera).Should().BeTrue();
        _ = SceneWire.HasComponent(node, EngineComponentId.SpotLight).Should().BeTrue();
        _ = SceneWire.HasComponent(node, EngineComponentId.OrthographicCamera).Should().BeFalse();
        _ = SceneWire.HasComponent(node, EngineComponentId.DirectionalLight).Should().BeFalse();
        _ = SceneWire.HasComponent(node, EngineComponentId.PointLight).Should().BeFalse();
        _ = SceneWire.HasComponent(node, EngineComponentId.LocalFogVolume).Should().BeFalse();
    }
}
