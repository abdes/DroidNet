// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// A scene file spells its references from the scene's identity: relative in its mount, by virtual path from another. The
/// model holds asset paths, so a scene read at one identity holds the same assets wherever the file spells them from.
/// </summary>
[TestClass]
[TestCategory("Unit")]
internal sealed class SceneFileReferencesTests
{
    private static readonly AssetPath Identity = AssetPath.Parse("/Content/Scenes/Main.oscene");

    [TestMethod]
    [DataRow("/Content/Geometry/Crate.ogeo", "../Geometry/Crate.ogeo", DisplayName = "In its mount")]
    [DataRow("/Content/Scenes/Crate.ogeo", "Crate.ogeo", DisplayName = "Beside it")]
    [DataRow("/Vendor/Geometry/Crate.ogeo", "/Vendor/Geometry/Crate.ogeo", DisplayName = "From a library")]
    [DataRow("/Engine/BasicShapes/Cube.ogeo", "/Engine/BasicShapes/Cube.ogeo", DisplayName = "From the engine")]
    public void AReferenceIsSpelledFromTheScene(string asset, string written)
    {
        var file = SceneSerializer.Write(SceneWithGeometry(AssetPath.Parse(asset)), Identity);

        _ = GeometryIn(JsonNode.Parse(file)!).GetValue<string>().Should().Be(written);
        _ = Encoding.UTF8.GetString(file).Should().NotContain(AssetPath.UriScheme + ":");
        _ = GeometryOf(SceneSerializer.Read(file, Identity)).Should().Be(AssetPath.Parse(asset));
    }

    [TestMethod]
    public void TheSameFileNamesOtherAssetsAtAnotherIdentity()
    {
        var file = SceneSerializer.Write(SceneWithGeometry(AssetPath.Parse("/Content/Geometry/Crate.ogeo")), Identity);

        var moved = SceneSerializer.Read(file, AssetPath.Parse("/Shared/Levels/One/Main.oscene"));

        _ = GeometryOf(moved).Should().Be(AssetPath.Parse("/Shared/Levels/Geometry/Crate.ogeo"));
    }

    [TestMethod]
    public void EveryKindOfReferenceIsSpelled()
    {
        var scene = SceneWithGeometry(AssetPath.Parse("/Content/Geometry/Crate.ogeo"));
        scene.SetEnvironment(scene.Environment with
        {
            SkySphere = scene.Environment.SkySphere with { Cubemap = AssetPath.Parse("/Content/Textures/Sky.otex") },
        });
        scene.SetStartingInputContexts([new StartingInputContextData(AssetPath.Parse("/Content/Input/Player.input/Play.oimap"))]);
        var slot = new ScriptSlot { Script = AssetPath.Parse("/Content/Scripts/Move.oscript") }
            .WithOverride("target", ScriptValue.FromAsset(AssetPath.Parse("/Vendor/Input/Jump.oiact")));
        _ = scene.RootNodes[0].AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot] });

        var file = SceneSerializer.Write(scene, Identity);

        var text = Encoding.UTF8.GetString(file);
        _ = text.Should().NotContain(AssetPath.UriScheme + ":");
        _ = text.Should().Contain("\"../Textures/Sky.otex\"").And.Contain("\"../Input/Player.input/Play.oimap\"")
            .And.Contain("\"../Scripts/Move.oscript\"").And.Contain("\"/Vendor/Input/Jump.oiact\"");
        var read = SceneSerializer.Read(file, Identity);
        _ = read.Environment.SkySphere.Cubemap.Should().Be(AssetPath.Parse("/Content/Textures/Sky.otex"));
        _ = read.StartingInputContexts.Should().ContainSingle().Which.Context.Should().Be(AssetPath.Parse("/Content/Input/Player.input/Play.oimap"));
        var readSlot = read.RootNodes[0].Components.OfType<ScriptingComponent>().Single().Slots[0];
        _ = readSlot.Script.Should().Be(AssetPath.Parse("/Content/Scripts/Move.oscript"));
        _ = readSlot.Overrides["target"].Asset.Should().Be(AssetPath.Parse("/Vendor/Input/Jump.oiact"));
    }

    [TestMethod]
    [DataRow("asset:///Content/Geometry/Crate.ogeo", DisplayName = "An asset URI")]
    [DataRow("/Content/Geometry/Crate.ogeo", DisplayName = "Its own mount by virtual path")]
    public void AReferenceTheGrammarRefusesNamesItsFieldAndCode(string written)
    {
        var root = JsonNode.Parse(SceneSerializer.Write(SceneWithGeometry(AssetPath.Parse("/Content/Geometry/Crate.ogeo")), Identity))!;
        GeometryIn(root).ReplaceWith(JsonValue.Create(written));
        var file = Encoding.UTF8.GetBytes(root.ToJsonString());

        Action read = () => _ = SceneSerializer.Read(file, Identity);

        _ = read.Should().Throw<JsonException>().WithMessage($"*GeometryUri*writes '{written}'*vpath.*");
    }

    [TestMethod]
    public void AStringThatIsNoReferenceIsLeftAlone()
    {
        var scene = SceneWithGeometry(AssetPath.Parse("/Content/Geometry/Crate.ogeo"));
        var slot = new ScriptSlot { Script = AssetPath.Parse("/Content/Scripts/Move.oscript") }
            .WithOverride("label", ScriptValue.FromString("/Content/Geometry/Crate.ogeo"));
        _ = scene.RootNodes[0].AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot] });

        var read = SceneSerializer.Read(SceneSerializer.Write(scene, Identity), Identity);

        _ = read.RootNodes[0].Components.OfType<ScriptingComponent>().Single().Slots[0].Overrides["label"].StringValue.Should().Be("/Content/Geometry/Crate.ogeo");
    }

    private static Scene SceneWithGeometry(AssetPath geometry)
    {
        var scene = new Scene { Name = "Main" };
        var node = new SceneNode(scene, "Crate");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = geometry });
        scene.RootNodes.Add(node);
        return scene;
    }

    private static AssetPath GeometryOf(Scene scene)
        => scene.RootNodes[0].Components.OfType<GeometryComponent>().Single().Geometry;

    private static JsonNode GeometryIn(JsonNode root)
        => root["RootNodes"]![0]!["Components"]!.AsArray().Single(component => (string?)component!["$type"] == "GeometryComponent")!["GeometryUri"]!;
}
