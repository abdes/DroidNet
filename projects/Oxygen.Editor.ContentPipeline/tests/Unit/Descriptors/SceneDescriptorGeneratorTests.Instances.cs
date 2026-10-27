// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Descriptors;

/// <summary>Checks that scene instances cook as the descriptor's <c>instance</c> of their root (scene instances 8.9).</summary>
internal sealed partial class SceneDescriptorGeneratorTests
{
    private const string WitnessedLayout = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    /// <summary>A model instance cooks as the model's scene product, and each change by node path, component kind and
    /// schema key in the schema's units, a surface's under its part.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ModelInstanceCooksItsSceneAndChanges()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        var door = NodeId.New();
        var surface = Guid.NewGuid();
        var changes = InstancedNodeChanges.None
            .With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind)
                .With(SceneSchema.Transform.PositionX, 2.5f)
                .With(SceneSchema.Transform.Rotation, new Vector3(0f, 70f, 0f)))
            .With(SourceOverrides.ChangesOf(SceneSchema.Geometry.Kind)
                .With(
                    SceneSchema.Geometry.Surface.Material,
                    new PartId(SceneSchema.Geometry.Surface.Kind, surface),
                    WitnessedLayout,
                    AssetPath.Parse("/Content/Paint/Red.omat")));
        var root = AddInstance(scene, "Car", AssetPath.Parse("/Content/Vehicles/Car.glb"), InstanceChanges.None.With(NodePath.Of(door), changes));

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        var node = JsonNode.Parse(descriptor.RootElement.GetProperty("nodes")[0].GetRawText())!;
        _ = node["id"]!.GetValue<string>().Should().Be(root.Id.ToString());
        var instance = node["instance"]!;
        _ = instance["scene_ref"]!.GetValue<string>().Should().Be("/Content/Vehicles/Car.glb/Car.oscene");
        var change = instance["overrides"]!.AsArray().Should().ContainSingle().Subject!;
        _ = change["path"]!.AsArray().Select(static id => id!.GetValue<string>()).Should().Equal(door.ToString());
        var transform = change["components"]!["transform"]!["values"]!;
        _ = transform["/local_position/0"]!.GetValue<float>().Should().Be(2.5f);
        _ = transform["/local_rotation_euler_degrees"]!.AsArray().Select(static axis => axis!.GetValue<float>()).Should().Equal(0f, 70f, 0f);
        var part = change["components"]!["geometry"]!["parts"]!.AsArray().Should().ContainSingle().Subject!;
        _ = part["kind"]!.GetValue<string>().Should().Be(SceneSchema.Geometry.Surface.Kind);
        _ = part["id"]!.GetValue<Guid>().Should().Be(surface);
        _ = part["layout_revision"]!.GetValue<string>().Should().Be(WitnessedLayout);
        _ = part["values"]!["/material"]!.GetValue<string>().Should().Be("/Content/Paint/Red.omat");
    }

    /// <summary>An instance of an authored scene cooks as that scene, and one without changes writes none.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneInstanceCooksTheSceneItself()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        _ = AddInstance(scene, "Tower", AssetPath.Parse("/Content/Prefabs/Watchtower.oscene"), InstanceChanges.None);

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        var instance = descriptor.RootElement.GetProperty("nodes")[0].GetProperty("instance");
        _ = instance.GetProperty("scene_ref").GetString().Should().Be("/Content/Prefabs/Watchtower.oscene");
        _ = instance.TryGetProperty("overrides", out _).Should().BeFalse();
    }

    /// <summary>A starting camera an instance brings in cooks as its instance root's index and the path below it.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InstancedStartingCameraCooksAsItsRootAndPath()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        _ = AddNode(scene, "Overview", camera: true);
        var rig = AddInstance(scene, "Rig", AssetPath.Parse("/Content/Prefabs/CameraRig.oscene"), InstanceChanges.None);
        var eye = NodeId.New();
        scene.SetStartingCamera(new NodeReference(rig.Id, NodePath.Of(eye)));

        using var descriptor = await this.GenerateDescriptorAsync(workspace, scene).ConfigureAwait(false);

        var camera = descriptor.RootElement.GetProperty("starting_camera");
        _ = camera.GetProperty("node").GetInt32().Should().Be(1);
        _ = camera.GetProperty("path").EnumerateArray().Select(static id => id.GetString()).Should().Equal(eye.ToString());
    }

    /// <summary>A source that is neither a model nor a scene fails the cook.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InstanceOfANonSceneSourceFails()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        _ = AddInstance(scene, "Wrong", AssetPath.Parse("/Content/Textures/Brick.png"), InstanceChanges.None);
        var generator = new SceneDescriptorGenerator();

        var result = await generator.GenerateAsync(scene, CreateScope(workspace), this.TestContext.CancellationToken).ConfigureAwait(false);

        var issue = result.Diagnostics.Should().ContainSingle().Which;
        _ = issue.Severity.Should().Be(DiagnosticSeverity.Error);
        _ = issue.Code.Should().Be(ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed);
    }

    /// <summary>The cook's warning about a kept change names the change by its last known names and leads to its instance
    /// root's kept changes (scene instances 8.10): the descriptor's projection keeps each node and change in order.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task KeptChangeWarningsOfTheCook_NameTheChangeAndItsInstanceRoot()
    {
        using var workspace = new DescriptorWorkspace();
        var scene = CreateScene();
        var door = NodePath.Of(NodeId.New());
        var changes = InstancedNodeChanges.None
            .With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 2.5f))
            .WithNames("Body/Door_FL");
        var root = AddInstance(scene, "Car", AssetPath.Parse("/Content/Vehicles/Car.glb"), InstanceChanges.None.With(door, changes));
        var scope = CreateScope(workspace);
        var result = await new SceneDescriptorGenerator().GenerateAsync(scene, scope, this.TestContext.CancellationToken).ConfigureAwait(false);
        var index = result.Nodes.Select(static (node, position) => (node, position)).Single(entry => entry.node.NodeId == root.Id).position;
        _ = result.Nodes[index].InstanceChanges.Should().Equal([new DescriptorInstanceChange(door, "Body/Door_FL")]);
        var issue = new DiagnosticRecord
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Warning,
            Code = KeptChangeDiagnostics.InstanceOverrideUnresolved,
            Message = "The change names no node of the instanced scene",
            TechnicalMessage = string.Create(CultureInfo.InvariantCulture, $"nodes[{index}].instance.overrides[0].components.transform"),
            AffectedPath = result.DescriptorPath,
        };

        var mapped = ContentPipelineService.MapKeptChangeDiagnostic(scope, [result], issue);

        _ = mapped.Code.Should().Be(issue.Code);
        _ = mapped.Message.Should().StartWith("Kept change of 'Body/Door_FL' in the instance under 'Car': ");
        _ = mapped.AffectedEntity!.NodeId.Should().Be(root.Id);
        _ = mapped.SuggestedAction!.Payload["NodeId"].Should().Be(root.Id.ToString());
        _ = mapped.SuggestedAction.Payload["PropertyPath"].Should().Be(KeptChangeDiagnostics.PropertyPath);

        // A lost surface is listed with the instanced node's surfaces: the warning selects that node.
        var surface = ContentPipelineService.MapKeptChangeDiagnostic(scope, [result], issue with { Code = KeptChangeDiagnostics.SurfaceInvalid });
        _ = surface.SuggestedAction!.Payload["NodeReference"].Should().Be(new NodeReference(root.Id, door).ToString());
        _ = surface.SuggestedAction.Payload["PropertyPath"].Should().BeEmpty();
    }

    private static SceneNode AddInstance(Scene scene, string name, AssetPath source, InstanceChanges changes)
    {
        var node = new SceneNode(scene, name);
        _ = node.AddComponent(new SceneInstanceComponent { Name = "Scene Instance", Source = source, Changes = changes });
        scene.RootNodes.Add(node);
        return node;
    }
}
