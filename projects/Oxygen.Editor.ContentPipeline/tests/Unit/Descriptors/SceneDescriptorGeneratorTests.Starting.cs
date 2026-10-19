// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Descriptors;

/// <summary>Checks that a scene's start state cooks as the player reads it.</summary>
public sealed partial class SceneDescriptorGeneratorTests
{
    private const string InputFile = """
        {
          "actions": [ { "name": "Jump", "type": "bool" } ],
          "contexts": [
            { "name": "Gameplay", "priority": 3, "mappings": [] },
            { "name": "Menu", "mappings": [] }
          ]
        }
        """;

    /// <summary>An authored starting camera cooks as its node index.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task AuthoredStartingCameraCooksAsItsNodeIndex()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        _ = AddNode(scene, "Ground", camera: false);
        _ = AddNode(scene, "Overview", camera: true);
        var gameplay = AddNode(scene, "Gameplay", camera: true);
        scene.SetStartingCamera(gameplay.Id);

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        _ = descriptor.RootElement.GetProperty("starting_camera").GetInt32().Should().Be(2);
    }

    /// <summary>An unset starting camera cooks as the first camera in hierarchy order, which the editor shows as
    /// Automatic; a scene without cameras cooks none.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task UnsetStartingCameraCooksTheFirstCameraInHierarchyOrder()
    {
        using var workspace = new DescriptorWorkspace();
        var empty = CreateScene();
        _ = AddNode(empty, "Ground", camera: false);
        using (var none = await this.GenerateDescriptorAsync(workspace, empty).ConfigureAwait(false))
        {
            _ = none.RootElement.TryGetProperty("starting_camera", out _).Should().BeFalse();
        }

        var scene = CreateScene();
        var rig = AddNode(scene, "Rig", camera: false);
        var nested = new SceneNode(scene) { Name = "Nested" };
        _ = nested.AddComponent(new PerspectiveCamera { Name = "Camera" });
        rig.AddChild(nested);
        _ = AddNode(scene, "Later", camera: true);

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        _ = descriptor.RootElement.GetProperty("starting_camera").GetInt32().Should().Be(1);
    }

    /// <summary>A starting camera that was deleted fails the cook with Go To Property on the field.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DeletedStartingCameraFailsWithGoToProperty()
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        var scene = CreateScene();
        var camera = AddNode(scene, "Gameplay", camera: true);
        scene.SetStartingCamera(camera.Id);
        _ = scene.RootNodes.Remove(camera);
        var generator = new SceneDescriptorGenerator();

        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        var issue = result.Diagnostics.Should().ContainSingle().Which;
        _ = issue.Severity.Should().Be(DiagnosticSeverity.Error);
        _ = issue.AffectedEntity!.SceneId.Should().Be(scene.Id);
        _ = issue.SuggestedAction!.Payload["PropertyPath"].Should().Be(SceneStartingPropertyPaths.StartingCamera);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    /// <summary>Starting input contexts cook in order, each with the priority it starts with: its override, else the
    /// priority its input source declares, else the schema default.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task StartingInputContextsCookInOrderWithTheirEffectivePriorities()
    {
        using var workspace = new DescriptorWorkspace();
        WriteInputFile(workspace);
        var scene = CreateScene();
        _ = AddNode(scene, "Camera", camera: true);
        scene.SetStartingInputContexts(
        [
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Controls.input/Menu.oimap")),
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Controls.input/Gameplay.oimap")),
            new StartingInputContextData(AssetPath.Parse("/Content/Input/Overlay.input/Overlay.oimap"), Priority: 7),
        ]);
        File.WriteAllText(
            Path.Combine(workspace.Root, "Content", "Input", "Overlay.input.json"),
            """{ "contexts": [ { "name": "Overlay", "priority": 1, "mappings": [] } ] }""");

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        var contexts = descriptor.RootElement.GetProperty("starting_input_contexts").EnumerateArray()
            .Select(static context => (context.GetProperty("context_ref").GetString(), context.GetProperty("priority").GetInt32()))
            .ToArray();
        _ = contexts.Should().Equal(
            ("/Content/Input/Controls.input/Menu.oimap", InputContextSources.DefaultPriority),
            ("/Content/Input/Controls.input/Gameplay.oimap", 3),
            ("/Content/Input/Overlay.input/Overlay.oimap", 7));
    }

    /// <summary>A starting input context that its input source does not declare fails the cook with Go To Property.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task UndeclaredStartingInputContextFailsWithGoToProperty()
    {
        using var workspace = new DescriptorWorkspace();
        var scope = CreateScope(workspace);
        WriteInputFile(workspace);
        var scene = CreateScene();
        _ = AddNode(scene, "Camera", camera: true);
        scene.SetStartingInputContexts([new StartingInputContextData(AssetPath.Parse("/Content/Input/Controls.input/Vehicle.oimap"))]);
        var generator = new SceneDescriptorGenerator();

        var result = await generator.GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);

        var issue = result.Diagnostics.Should().ContainSingle().Which;
        _ = issue.Message.Should().Contain("Vehicle");
        _ = issue.SuggestedAction!.Payload["PropertyPath"].Should().Be(SceneStartingPropertyPaths.StartingInputContexts);
        _ = File.Exists(result.DescriptorPath).Should().BeFalse();
    }

    private static SceneNode AddNode(Scene scene, string name, bool camera)
    {
        var node = new SceneNode(scene) { Name = name };
        if (camera)
        {
            _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        }

        scene.RootNodes.Add(node);
        return node;
    }

    private static void WriteInputFile(DescriptorWorkspace workspace)
    {
        var folder = Path.Combine(workspace.Root, "Content", "Input");
        _ = Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "Controls.input.json"), InputFile);
    }

    private async Task<JsonDocument> GenerateDescriptorAsync(DescriptorWorkspace workspace, Scene scene)
    {
        var generator = new SceneDescriptorGenerator();
        var result = await generator.GenerateAsync(scene, CreateScope(workspace), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().BeEmpty();
        return JsonDocument.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false));
    }
}
