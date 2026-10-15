// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks how a scene records the way it starts when played.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Play")]
public class ScenePlayTests
{
    private readonly Mock<IProject> projectMock = new();

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public async Task Play_RoundTripsAndStaysOmittedWhileDefault()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Play Round Trip" };
        var camera = AddCamera(scene, "Gameplay Camera");
        var play = new ScenePlayData
        {
            StartingCamera = camera.Id,
            StartingInputContexts =
            [
                new StartingInputContextData(new Uri("asset:///Content/Input/Gameplay.oimap"), Priority: 5),
                new StartingInputContextData(new Uri("asset:///Content/Input/Menu.oimap")),
            ],
        };
        scene.SetPlay(play);
        var serializer = new SceneSerializer(this.ExampleProject);
        using var stream = new MemoryStream();

        await serializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        stream.Position = 0;
        var restored = await serializer.DeserializeAsync(stream).ConfigureAwait(false);

        _ = ScenePlayData.AreEquivalent(restored.Play, play).Should().BeTrue();
        _ = restored.Play.StartingInputContexts[1].Priority.Should().BeNull();
        using var defaultStream = new MemoryStream();
        await serializer.SerializeAsync(defaultStream, new Scene(this.ExampleProject) { Name = "Default Play" }).ConfigureAwait(false);
        _ = Encoding.UTF8.GetString(defaultStream.ToArray()).Should().NotContain("\"Play\"");
    }

    [TestMethod]
    public void EffectiveStartingCamera_IsTheAuthoredCameraElseTheFirstInHierarchyOrder()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Cameras" };
        _ = EffectiveCameraName(scene).Should().BeNull();

        var group = new SceneNode(scene) { Name = "Group" };
        scene.RootNodes.Add(group);
        var nested = new SceneNode(scene) { Name = "Nested Camera" };
        _ = nested.AddComponent(new PerspectiveCamera { Name = "Camera" });
        group.AddChild(nested);
        var later = AddCamera(scene, "Later Camera");

        _ = EffectiveCameraName(scene).Should().Be("Nested Camera");

        scene.SetPlay(scene.Play with { StartingCamera = later.Id });
        _ = EffectiveCameraName(scene).Should().Be("Later Camera");

        scene.SetPlay(scene.Play with { StartingCamera = group.Id });
        _ = scene.EffectiveStartingCamera.Should().BeNull("an authored node without a camera starts nothing, and the cook reports it");
    }

    [TestMethod]
    public void Play_ContextListCannotBeMutatedOutsideSceneCommands()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Play" };
        scene.SetPlay(new ScenePlayData { StartingInputContexts = [new StartingInputContextData(new Uri("asset:///Content/Input/Gameplay.oimap"))] });

        var edit = () => scene.Play.StartingInputContexts.Add(new StartingInputContextData(new Uri("asset:///Content/Input/Menu.oimap")));

        _ = edit.Should().Throw<NotSupportedException>();
        _ = scene.Play.StartingInputContexts.Should().ContainSingle();
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
