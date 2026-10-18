// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks how a scene records the way it starts: its starting camera and its starting input contexts.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Starting")]
public class SceneStartingTests
{
    [TestMethod]
    public async Task Starting_RoundTripsAndStaysOmittedWhileDefault()
    {
        var scene = new Scene { Name = "Starting Round Trip" };
        var camera = AddCamera(scene, "Gameplay Camera");
        StartingInputContextData[] contexts =
        [
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Gameplay.oimap"), Priority: 5),
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Menu.oimap")),
        ];
        scene.SetStartingCamera(camera.Id);
        scene.SetStartingInputContexts(contexts);
        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);

        await SceneSerializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        stream.Position = 0;
        var restored = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = restored.StartingCamera.Should().Be(camera.Id);
        _ = restored.StartingInputContexts.Should().Equal(contexts);
        _ = restored.StartingInputContexts[1].Priority.Should().BeNull();
        var defaultStream = new MemoryStream();
        await using var defaultLifetime = defaultStream.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(defaultStream, new Scene { Name = "Default Start" }).ConfigureAwait(false);
        var json = Encoding.UTF8.GetString(defaultStream.ToArray());
        _ = json.Should().NotContain("\"StartingCamera\"").And.NotContain("\"StartingInputContexts\"");
    }

    [TestMethod]
    public void EffectiveStartingCamera_IsTheAuthoredCameraElseTheFirstInHierarchyOrder()
    {
        var scene = new Scene { Name = "Cameras" };
        _ = EffectiveCameraName(scene).Should().BeNull();

        var group = new SceneNode(scene) { Name = "Group" };
        scene.RootNodes.Add(group);
        var nested = new SceneNode(scene) { Name = "Nested Camera" };
        _ = nested.AddComponent(new PerspectiveCamera { Name = "Camera" });
        group.AddChild(nested);
        var later = AddCamera(scene, "Later Camera");

        _ = EffectiveCameraName(scene).Should().Be("Nested Camera");

        scene.SetStartingCamera(later.Id);
        _ = EffectiveCameraName(scene).Should().Be("Later Camera");

        scene.SetStartingCamera(group.Id);
        _ = scene.EffectiveStartingCamera.Should().BeNull("an authored node without a camera starts nothing, and the cook reports it");
    }

    [TestMethod]
    public void StartingInputContexts_CannotBeMutatedOutsideSceneCommands()
    {
        var scene = new Scene { Name = "Starting" };
        scene.SetStartingInputContexts([new StartingInputContextData(AssetPath.Parse("/Content/Input/Gameplay.oimap"))]);

        var edit = () => ((IList<StartingInputContextData>)scene.StartingInputContexts).Add(new StartingInputContextData(AssetPath.Parse("/Content/Input/Menu.oimap")));

        _ = edit.Should().Throw<NotSupportedException>();
        _ = scene.StartingInputContexts.Should().ContainSingle();
    }

    private static SceneNode AddCamera(Scene scene, string name)
    {
        var node = new SceneNode(scene) { Name = name };
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        scene.RootNodes.Add(node);
        return node;
    }

    private static string? EffectiveCameraName(Scene scene) => scene.EffectiveStartingCamera?.Name;
}
