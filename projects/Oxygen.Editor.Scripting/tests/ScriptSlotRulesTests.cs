// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting.Tests;

/// <summary>
/// A slot stores only the values that differ from its script's defaults, a value of another type is stale, and the
/// slot problems the inspector and the cook report carry their code and the field they lead to.
/// </summary>
[TestClass]
public sealed class ScriptSlotRulesTests
{
    private static readonly AssetPath Move = AssetPath.Parse("/Content/Player/move.oscript");
    private static readonly ScriptParameter Speed = new() { Name = "speed", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(1f) };
    private static readonly ScriptParameter Follow = new() { Name = "follow", Kind = ScriptParameterKind.NodeParameter };
    private static readonly ScriptParameter Look = new() { Name = "look", Kind = ScriptParameterKind.AssetParameter, AssetType = (byte)AssetType.Material };

    private static readonly Script MoveScript = new() { Identity = Move, Origin = ScriptOrigin.Source, Parameters = [Speed, Follow, Look] };

    [TestMethod]
    public void AValueTheScriptDoesNotDeclareWithThatTypeIsStale()
    {
        var slot = new ScriptSlot { Script = Move }
            .WithOverride("speed", ScriptValue.FromFloat(2f))
            .WithOverride("gone", ScriptValue.FromBool(value: true))
            .WithOverride("follow", ScriptValue.FromInt(3));

        _ = ScriptSlotRules.FindStale(slot, MoveScript.Parameters).Should().Equal("follow", "gone");
    }

    [TestMethod]
    public void TheDeclaredDefaultIsNotStored()
    {
        _ = ScriptSlotRules.IsDefault(Speed, ScriptValue.FromFloat(1f)).Should().BeTrue();
        _ = ScriptSlotRules.IsDefault(Speed, value: null).Should().BeTrue();
        _ = ScriptSlotRules.IsDefault(Speed, ScriptValue.FromFloat(2f)).Should().BeFalse();
        _ = ScriptSlotRules.IsDefault(Follow, ScriptValue.FromNode(NodeId.New())).Should().BeFalse("a node parameter defaults to none");
    }

    [TestMethod]
    public void AValueMustHaveTheDeclaredTypeAndNameAnAssetOfTheDeclaredType()
    {
        var slot = Guid.NewGuid();

        _ = ScriptSlotRules.CheckValue(slot, Speed, ScriptValue.FromFloat(4f)).Should().BeNull();
        var kind = ScriptSlotRules.CheckValue(slot, Speed, ScriptValue.FromBool(value: true))!;
        _ = kind.Code.Should().Be(ScriptDiagnosticCodes.SlotValueKindMismatch);
        _ = kind.Field.Should().Be(new ScriptFieldAddress(slot, "speed"));
        _ = ScriptSlotRules.CheckValue(slot, Look, ScriptValue.FromAsset(AssetPath.Parse("/Content/Red.omat"))).Should().BeNull();
        _ = ScriptSlotRules.CheckValue(slot, Look, ScriptValue.FromAsset(AssetPath.Parse("/Content/Run.oscript")))!.Code.Should().Be(ScriptDiagnosticCodes.SlotAssetTypeMismatch);
    }

    [TestMethod]
    public void ASlotWithoutAScriptOrWithAMissingOneHasOnlyThatProblem()
    {
        var empty = new ScriptSlot().WithOverride("follow", ScriptValue.FromNode(NodeId.New()));
        var gone = new ScriptSlot { Script = AssetPath.Parse("/Content/Player/gone.oscript") }.WithOverride("follow", ScriptValue.FromNode(NodeId.New()));

        var without = ScriptSlotRules.Check(empty, static _ => false, static _ => true).Should().ContainSingle().Subject;
        var missing = ScriptSlotRules.Check(gone, static _ => false, script => script != gone.Script).Should().ContainSingle().Subject;

        _ = (without.Code, without.Field).Should().Be((ScriptDiagnosticCodes.SlotWithoutScript, new ScriptFieldAddress(empty.Id)));
        _ = (missing.Code, missing.Field).Should().Be((ScriptDiagnosticCodes.SlotScriptMissing, new ScriptFieldAddress(gone.Id)));
    }

    [TestMethod]
    public void EachValueIsCheckedAgainstTheSceneAndTheKnownScript()
    {
        var target = NodeId.New();
        var slot = new ScriptSlot { Script = Move }
            .WithOverride("follow", ScriptValue.FromNode(NodeId.New()))
            .WithOverride("speed", ScriptValue.FromBool(value: true))
            .WithOverride("look", ScriptValue.FromNode(target));

        var unknown = ScriptSlotRules.Check(slot, node => node == target, static _ => true);
        var known = ScriptSlotRules.Check(slot, node => node == target, static _ => true, MoveScript);

        _ = unknown.Select(static problem => (problem.Code, problem.Field.Parameter)).Should().Equal((ScriptDiagnosticCodes.SlotNodeMissing, "follow"));
        _ = known.Select(static problem => (problem.Code, problem.Field.Parameter)).Should().BeEquivalentTo(
            [(ScriptDiagnosticCodes.SlotNodeMissing, "follow"), (ScriptDiagnosticCodes.SlotValueKindMismatch, "look"), (ScriptDiagnosticCodes.SlotValueKindMismatch, "speed")]);
    }

    [TestMethod]
    public void AFieldAddressRoundTripsThroughItsText()
    {
        var slot = Guid.NewGuid();

        _ = ScriptFieldAddress.TryParse(new ScriptFieldAddress(slot, "speed").ToString(), out var field).Should().BeTrue();
        _ = field.Should().Be(new ScriptFieldAddress(slot, "speed"));
        _ = ScriptFieldAddress.TryParse(new ScriptFieldAddress(slot).ToString(), out var whole).Should().BeTrue();
        _ = whole.Parameter.Should().BeNull();
        _ = ScriptFieldAddress.TryParse("Environment/Sky", out _).Should().BeFalse();
        _ = ScriptFieldAddress.TryParse("Scripting/not-a-slot", out _).Should().BeFalse();
    }
}
