// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>Checks the scene instance component's contract (scene instances 8.1).</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneInstanceComponent))]
public sealed class SceneInstanceComponentTests
{
    private const string Layout = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Red = AssetPath.Parse("/Content/Paint/Red.omat");
    private static readonly NodeId Body = NodeId.FromValue(Guid.Parse("91aa0000-0000-4000-8000-000000000001"));
    private static readonly NodeId Door = NodeId.FromValue(Guid.Parse("5d0c0000-0000-4000-8000-000000000002"));
    private static readonly Guid Paint = Guid.Parse("7e570000-0000-4000-8000-000000000003");

    /// <summary>A scene with an instance and its changes saves and loads with its source and every change.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneWithAnInstance_RoundTripsItsSourceAndChanges()
    {
        var scene = CreateSceneWithCar(out var instance);

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        var loaded = restored.RootNodes.Should().ContainSingle().Subject.Components.OfType<SceneInstanceComponent>().Should().ContainSingle().Subject;
        _ = loaded.Source.Should().Be(Car);
        _ = loaded.Changes.Should().Be(instance.Changes);
        _ = loaded.Changes.Of(NodePath.Of(Body, Door)).Names.Should().Be("Body/Door_FL");
    }

    /// <summary>The saved form names the component <c>SceneInstance</c> and lists each change by node path and component
    /// kind, with the overrides' own form.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SavedInstance_HasTheDocumentedShape()
    {
        var scene = CreateSceneWithCar(out _);

        var json = await SaveAsync(scene).ConfigureAwait(false);

        var component = json["RootNodes"]![0]!["Components"]!.AsArray()
            .Single(static entry => string.Equals(entry!["$type"]!.GetValue<string>(), "SceneInstance", StringComparison.Ordinal))!;
        _ = component["Source"]!.GetValue<string>().Should().Be(Car.ToUriString());
        var changes = component["Changes"]!.AsArray();
        _ = changes.Should().HaveCount(2);
        var door = changes.Single(static entry => entry!["Path"]!.AsArray().Count == 2)!;
        _ = door["Path"]!.AsArray().Select(static id => id!.GetValue<string>()).Should().Equal(Body.ToString(), Door.ToString());
        _ = door["Components"]!["transform"]!["Values"]!["/local_rotation_euler_degrees"].Should().NotBeNull();
        var body = changes.Single(static entry => entry!["Path"]!.AsArray().Count == 1)!;
        var part = body["Components"]!["geometry"]!["Parts"]![0]!;
        _ = part["Values"]!["/material"]!.GetValue<string>().Should().Be(Red.ToUriString());
    }

    /// <summary>Relocation finds the instanced source and every asset a change names.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ReferenceFields_ReachTheSourceAndChangedAssets()
    {
        var scene = CreateSceneWithCar(out _);
        var json = await SaveAsync(scene).ConfigureAwait(false);

        var references = SceneReferenceFields.All.SelectMany(field => field.Select(json)).Select(static value => value.GetValue<string>()).ToList();

        _ = references.Should().Contain(Car.ToUriString()).And.Contain(Red.ToUriString());
    }

    /// <summary>An instanced node's changes may change any property with an override id, which a source's overrides may
    /// not.</summary>
    [TestMethod]
    public void InstanceChanges_TakeEveryChangeableProperty()
    {
        var changes = SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 2f);
        var overrides = () => SourceOverrides.For(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 2f);

        _ = changes.TryGet(SceneSchema.Transform.PositionX, out var x).Should().BeTrue();
        _ = x.Should().Be(2f);
        _ = changes.IsInstanceChanges.Should().BeTrue();
        _ = overrides.Should().Throw<ArgumentException>();
    }

    /// <summary>A node's changes are an instanced node's changes, and name a node below the instance root.</summary>
    [TestMethod]
    public void Changes_RefuseASourcesOverridesAndTheRootItself()
    {
        var overrides = SourceOverrides.For(SceneSchema.Geometry.Kind);
        var root = () => InstanceChanges.None.With(NodePath.Empty, InstancedNodeChanges.None.With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 1f)));
        var source = () => InstancedNodeChanges.None.With(overrides);

        _ = source.Should().Throw<ArgumentException>();
        _ = root.Should().Throw<ArgumentException>();
    }

    /// <summary>Relocating an asset rewrites the source and every changed asset; others stay.</summary>
    [TestMethod]
    public void MapAssets_RewritesChangedAssets()
    {
        _ = CreateSceneWithCar(out var instance);
        var blue = AssetPath.Parse("/Content/Paint/Blue.omat");

        var mapped = instance.Changes.MapAssets(asset => asset.Equals(Red) ? blue : asset);

        _ = mapped.Assets.Should().Equal(blue);
        _ = instance.Changes.Assets.Should().Equal(Red);
    }

    private static Scene CreateSceneWithCar(out SceneInstanceComponent instance)
    {
        var scene = new Scene { Name = "Harbor" };
        var node = new SceneNode(scene, "Car3");
        var door = InstancedNodeChanges.None
            .With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.Rotation, new Vector3(0f, 70f, 0f)))
            .WithNames("Body/Door_FL");
        var body = InstancedNodeChanges.None
            .With(SourceOverrides.ChangesOf(SceneSchema.Geometry.Kind)
                .With(SceneSchema.Geometry.Surface.Material, new PartId(SceneSchema.Geometry.Surface.Kind, Paint), Layout, Red))
            .WithNames("Body");
        instance = new SceneInstanceComponent
        {
            Name = "Scene Instance",
            Source = Car,
            Changes = InstanceChanges.None.With(NodePath.Of(Body, Door), door).With(NodePath.Of(Body), body),
        };
        _ = node.AddComponent(instance);
        scene.RootNodes.Add(node);
        return scene;
    }

    private static async Task<JsonNode> SaveAsync(Scene scene)
    {
        var stream = new MemoryStream();
        await using var lifetime = stream.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(stream, scene).ConfigureAwait(false);
        return JsonNode.Parse(Encoding.UTF8.GetString(stream.ToArray()))!;
    }
}
