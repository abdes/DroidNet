// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies scripting component serialization, slot ordering, default omissions, and value validation.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ScriptingComponent))]
public sealed class ScriptingComponentTests
{
    private static readonly AssetPath MoveScript = AssetPath.Parse("/Content/Player/move.oscript");
    private static readonly AssetPath LookScript = AssetPath.Parse("/Content/Player/look.oscript");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task SaveAndReopenKeepSlotsOrderExecutionOrderAndEveryValueType()
    {
        var scene = new Scene { Name = "Scripted" };
        var target = new SceneNode(scene) { Name = "Target" };
        var player = new SceneNode(scene) { Name = "Player" };
        scene.RootNodes.Add(target);
        scene.RootNodes.Add(player);
        var first = new ScriptSlot { Script = MoveScript, ExecutionOrder = -5 }
            .WithOverride("enabled", ScriptValue.FromBool(value: true))
            .WithOverride("count", ScriptValue.FromInt(-3))
            .WithOverride("speed", ScriptValue.FromFloat(2.5f))
            .WithOverride("label", ScriptValue.FromString("hero"))
            .WithOverride("offset", ScriptValue.FromVector(new Vector2(1, 2)))
            .WithOverride("tint", ScriptValue.FromVector(new Vector3(0.1f, 0.2f, 0.3f)))
            .WithOverride("rect", ScriptValue.FromVector(new Vector4(1, 2, 3, 4)))
            .WithOverride("jump", ScriptValue.FromAsset(AssetPath.Parse("/Content/Input/Jump.oiact")))
            .WithOverride("follow", ScriptValue.FromNode(target.Id));
        var second = new ScriptSlot { Script = LookScript, ExecutionOrder = 7 };
        var unassigned = new ScriptSlot();
        var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [second, first, unassigned] };
        _ = player.AddComponent(scripting);

        var reopened = await RoundTripAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);

        var slots = reopened.AllNodes.Single(node => node.Id == player.Id).Components.OfType<ScriptingComponent>().Single().Slots;
        _ = slots.Should().Equal(second, first, unassigned);
        _ = slots[1].Overrides["follow"].NodeId.Should().Be(target.Id);
        _ = slots[1].Overrides["offset"].GetVectorComponents().Should().Equal(1f, 2f);
    }

    [TestMethod]
    public void DefaultsAreNotWritten()
    {
        var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot { Script = MoveScript }] };

        var json = JsonSerializer.Serialize(scripting.Dehydrate(), SceneJsonContext.Default.ComponentData);

        _ = json.Should().NotContain("ExecutionOrder").And.NotContain("Overrides").And.Contain("\"$type\": \"Scripting\"");
    }

    [TestMethod]
    [DataRow("float", "\"fast\"")]
    [DataRow("vec3", "[1, 2]")]
    [DataRow("node", "\"not-a-node\"")]
    [DataRow("asset", "\"relative/path\"")]
    [DataRow("matrix", "1")]
    public void InvalidValueIsRejected(string type, string value)
    {
        var json = $$"""
            { "$type": "Scripting", "Name": "Scripting", "Slots": [ { "Id": "0f8fad5b-d9cb-469f-a165-70867728950e",
              "Overrides": { "speed": { "Type": "{{type}}", "Value": {{value}} } } } ] }
            """;
        var data = JsonSerializer.Deserialize(json, SceneJsonContext.Default.ComponentData)!;

        var hydrate = () => GameComponent.CreateAndHydrate(data);

        _ = hydrate.Should().Throw<JsonException>().WithMessage("*'speed'*");
    }

    [TestMethod]
    public void SlotEqualityComparesOverrideValues()
    {
        var id = Guid.NewGuid();
        var left = new ScriptSlot { Id = id, Script = MoveScript }.WithOverride("speed", ScriptValue.FromFloat(1));
        var same = new ScriptSlot { Id = id, Script = MoveScript }.WithOverride("speed", ScriptValue.FromFloat(1));
        var other = same.WithOverride("speed", ScriptValue.FromFloat(2));

        _ = left.Should().Be(same);
        _ = left.Should().NotBe(other);
        _ = left.WithoutOverrides(["speed"]).Overrides.Should().BeEmpty();
    }

    private static async Task<Scene> RoundTripAsync(Scene scene, CancellationToken cancellationToken)
    {
        var saved = new MemoryStream();
        await using var savedLifetime = saved.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(saved, scene).ConfigureAwait(false);
        var reopened = new MemoryStream(Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(saved.ToArray())));
        await using var reopenedLifetime = reopened.ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        return await SceneSerializer.DeserializeAsync(reopened).ConfigureAwait(false);
    }
}
