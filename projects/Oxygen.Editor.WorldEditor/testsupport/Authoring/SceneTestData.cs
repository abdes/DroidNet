// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Color = Windows.UI.Color;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class SceneTestData
{
    internal static Vector3 ReadSceneColor(SceneAuthoringFixture fixture, string kind) => string.Equals(kind, "Environment", StringComparison.Ordinal) ? fixture.Scene.Environment.BackgroundColor : fixture.Node.Components.OfType<DirectionalLightComponent>().Single().Color;

    internal static void AssertColorClose(Vector3 actual, Vector3 expected)
    {
        _ = actual.X.Should().BeApproximately(expected.X, 0.000001f);
        _ = actual.Y.Should().BeApproximately(expected.Y, 0.000001f);
        _ = actual.Z.Should().BeApproximately(expected.Z, 0.000001f);
    }

    internal static void MakeGeometryOnly(SceneAuthoringFixture fixture)
    {
        foreach (var component in fixture.Node.Components.Where(component => component is not TransformComponent).ToArray())
        {
            _ = fixture.Node.RemoveComponent(component);
        }

        _ = fixture.Node.AddComponent(CreateInspectorGeometry("Custom instance name"));
    }

    internal static GeometryComponent CreateInspectorGeometry(string name = "Geometry") => new()
    {
        Name = name,
        Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
    };

    internal static SceneNode AddNumericNode(Scene scene, float value)
    {
        var node = new SceneNode(scene)
        {
            Name = "Other target"
        };
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera", FieldOfView = value });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Light", IntensityLux = value });
        scene.RootNodes.Add(node);
        return node;
    }

    internal static float ReadNumericSource(SceneNode node, string kind) => string.Equals(kind, "Camera", StringComparison.Ordinal) ? node.Components.OfType<PerspectiveCamera>().Single().FieldOfView : node.Components.OfType<DirectionalLightComponent>().Single().IntensityLux;

    internal static void SetNumericSource(SceneNode node, string kind, float value)
    {
        if (string.Equals(kind, "Camera", StringComparison.Ordinal))
        {
            node.Components.OfType<PerspectiveCamera>().Single().FieldOfView = value;
        }
        else
        {
            node.Components.OfType<DirectionalLightComponent>().Single().IntensityLux = value;
        }
    }
}
