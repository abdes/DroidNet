// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The assertions every class shares. Diagnostics are matched by code, node and property, never by message.
/// </summary>
public static class SceneAssert
{
    /// <summary>
    /// Asserts that an edit applied with no diagnostic at all.
    /// </summary>
    /// <param name="result">The result.</param>
    public static void Applied(SceneEditResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied, Describe(result));
        _ = result.Diagnostics.Should().BeEmpty();
    }

    /// <summary>
    /// Asserts that an edit applied with exactly one diagnostic: a warning with this code and node.
    /// </summary>
    /// <param name="result">The result.</param>
    /// <param name="code">The warning's <see cref="SceneEditCodes"/> code.</param>
    /// <param name="node">The warning's node, or <see langword="null"/> when it names none.</param>
    public static void Warned(SceneEditResult result, string code, NodeId? node = null)
    {
        ArgumentNullException.ThrowIfNull(result);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied, Describe(result));
        Single(result.Diagnostics, isError: false, code, node, property: null);
    }

    /// <summary>
    /// Asserts that an edit was refused with exactly one error with this code, node and property.
    /// </summary>
    /// <param name="result">The result.</param>
    /// <param name="code">The error's <see cref="SceneEditCodes"/> code.</param>
    /// <param name="node">The error's node, or <see langword="null"/> when it names none.</param>
    /// <param name="property">The error's property, or <see langword="null"/> when it names none.</param>
    public static void Refused(SceneEditResult result, string code, NodeId? node = null, PropertyId? property = null)
    {
        ArgumentNullException.ThrowIfNull(result);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Refused, Describe(result));
        Single(result.Diagnostics, isError: true, code, node, property);
    }

    /// <summary>
    /// Asserts that an edit was invalid with exactly one error with this code, node and property.
    /// </summary>
    /// <param name="result">The result.</param>
    /// <param name="code">The error's <see cref="SceneEditCodes"/> code.</param>
    /// <param name="node">The error's node, or <see langword="null"/> when it names none.</param>
    /// <param name="property">The error's property, or <see langword="null"/> when it names none.</param>
    public static void Invalid(SceneEditResult result, string code, NodeId? node = null, PropertyId? property = null)
    {
        ArgumentNullException.ThrowIfNull(result);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Invalid, Describe(result));
        Single(result.Diagnostics, isError: true, code, node, property);
    }

    /// <summary>
    /// Asserts that a validation was refused with exactly one error with this code, node and property.
    /// </summary>
    /// <param name="validation">The validation.</param>
    /// <param name="code">The error's <see cref="SceneEditCodes"/> code.</param>
    /// <param name="node">The error's node, or <see langword="null"/> when it names none.</param>
    /// <param name="property">The error's property, or <see langword="null"/> when it names none.</param>
    public static void Refused(SceneEditValidation validation, string code, NodeId? node = null, PropertyId? property = null)
    {
        ArgumentNullException.ThrowIfNull(validation);
        _ = validation.Outcome.Should().Be(SceneEditOutcome.Refused, Describe(validation.Diagnostics));
        Single(validation.Diagnostics, isError: true, code, node, property);
    }

    /// <summary>
    /// Asserts that a validation was invalid with exactly one error with this code, node and property.
    /// </summary>
    /// <param name="validation">The validation.</param>
    /// <param name="code">The error's <see cref="SceneEditCodes"/> code.</param>
    /// <param name="node">The error's node, or <see langword="null"/> when it names none.</param>
    /// <param name="property">The error's property, or <see langword="null"/> when it names none.</param>
    public static void Invalid(SceneEditValidation validation, string code, NodeId? node = null, PropertyId? property = null)
    {
        ArgumentNullException.ThrowIfNull(validation);
        _ = validation.Outcome.Should().Be(SceneEditOutcome.Invalid, Describe(validation.Diagnostics));
        Single(validation.Diagnostics, isError: true, code, node, property);
    }

    /// <summary>
    /// Asserts that an edit found its scene retired.
    /// </summary>
    /// <param name="result">The result.</param>
    public static void Retired(SceneEditResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Retired, Describe(result));
        Single(result.Diagnostics, isError: true, SceneEditCodes.Retired, node: null, property: null);
    }

    /// <summary>
    /// Asserts that the scene is as it was: same document, same hidden nodes, same history, no new <c>Changed</c>.
    /// </summary>
    /// <param name="fixture">The fixture.</param>
    /// <param name="before">The snapshot taken before the act step.</param>
    public static void Unchanged(AuthoringFixture fixture, SceneSnapshot before)
    {
        ArgumentNullException.ThrowIfNull(fixture);
        ArgumentNullException.ThrowIfNull(before);
        var now = fixture.Snapshot();
        _ = now.Document.Should().Be(before.Document, "the document must be unchanged");
        _ = now.Hidden.Should().Be(before.Hidden, "the hidden nodes must be unchanged");
        _ = now.UndoCount.Should().Be(before.UndoCount, "nothing must be recorded");
        _ = now.RedoCount.Should().Be(before.RedoCount, "the redo stack must be kept");
        _ = now.ChangedCount.Should().Be(before.ChangedCount, "no Changed must be raised");
    }

    /// <summary>
    /// Asserts that changes are exactly these, in this order, compared by value.
    /// </summary>
    /// <param name="actual">The changes raised.</param>
    /// <param name="expected">The changes expected.</param>
    public static void Changes(IReadOnlyList<SceneChange> actual, params IReadOnlyList<SceneChange> expected)
    {
        ArgumentNullException.ThrowIfNull(actual);
        ArgumentNullException.ThrowIfNull(expected);

        // Equivalency compares members, so two kinds with the same members would pass without the kinds checked first.
        _ = actual.Select(static change => change.GetType().Name).Should().Equal(expected.Select(static change => change.GetType().Name));

        // The kinds without members (LayoutChanged, StartingCameraChanged, StartingInputContextsChanged,
        // SceneReferencesChanged) have nothing to compare by members: the kind check above is their whole comparison.
        _ = actual.Should().BeEquivalentTo(
            expected,
            static options => options
                .WithStrictOrdering()
                .PreferringRuntimeMemberTypes()
                .ComparingByValue<SceneChange.LayoutChanged>()
                .ComparingByValue<SceneChange.StartingCameraChanged>()
                .ComparingByValue<SceneChange.StartingInputContextsChanged>()
                .ComparingByValue<SceneChange.SceneReferencesChanged>());
    }

    /// <summary>
    /// Executes an edit and asserts its round trip: execute gives state A; undo gives the state before; redo gives
    /// A; undo gives the state before again. The state is the document and the hidden nodes, so an edit that only
    /// changes the editor's view round-trips too. Each step raises one <c>Changed</c> event.
    /// </summary>
    /// <param name="fixture">The fixture.</param>
    /// <param name="edit">The edit; it must change the document or the hidden nodes.</param>
    /// <returns>The execute result, which applied.</returns>
    public static async Task<SceneEditResult> RoundTripAsync(AuthoringFixture fixture, ISceneEdit edit)
    {
        ArgumentNullException.ThrowIfNull(fixture);
        ArgumentNullException.ThrowIfNull(edit);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(true);
        _ = result.Outcome.Should().Be(SceneEditOutcome.Applied, Describe(result));
        var applied = fixture.Snapshot();
        _ = StateOf(applied).Should().NotBe(StateOf(before), "a round trip needs an edit that changes the scene");
        Step(applied, before, "execute");

        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(true)).Outcome.Should().Be(SceneEditOutcome.Applied, "undo must succeed");
        var undone = fixture.Snapshot();
        _ = StateOf(undone).Should().Be(StateOf(before), "undo must restore the scene");
        Step(undone, applied, "undo");

        _ = (await fixture.Authoring.RedoAsync().ConfigureAwait(true)).Outcome.Should().Be(SceneEditOutcome.Applied, "redo must succeed");
        var redone = fixture.Snapshot();
        _ = StateOf(redone).Should().Be(StateOf(applied), "redo must restore the edited scene");
        Step(redone, undone, "redo");

        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(true)).Outcome.Should().Be(SceneEditOutcome.Applied, "the second undo must succeed");
        var again = fixture.Snapshot();
        _ = StateOf(again).Should().Be(StateOf(before), "the second undo must restore the scene");
        Step(again, redone, "the second undo");
        return result;
    }

    /// <summary>
    /// Asserts that two rotations are the same, whatever the sign of their quaternions.
    /// </summary>
    /// <param name="actual">The actual rotation.</param>
    /// <param name="expected">The expected rotation.</param>
    public static void SameRotation(Quaternion actual, Quaternion expected)
        => _ = MathF.Abs(Quaternion.Dot(Quaternion.Normalize(actual), Quaternion.Normalize(expected)))
            .Should().BeGreaterThanOrEqualTo(1 - 1e-6f, $"{actual} must be the same rotation as {expected}");

    /// <summary>
    /// Asserts that two positions are the same within 1e-5 on each axis.
    /// </summary>
    /// <param name="actual">The actual position.</param>
    /// <param name="expected">The expected position.</param>
    public static void SamePosition(Vector3 actual, Vector3 expected)
    {
        _ = actual.X.Should().BeApproximately(expected.X, 1e-5f);
        _ = actual.Y.Should().BeApproximately(expected.Y, 1e-5f);
        _ = actual.Z.Should().BeApproximately(expected.Z, 1e-5f);
    }

    // What a round trip restores: the document and the editor's hidden nodes.
    private static string StateOf(SceneSnapshot snapshot) => $"{snapshot.Document}|hidden: {snapshot.Hidden}";

    private static void Step(SceneSnapshot after, SceneSnapshot before, string step)
        => _ = after.ChangedCount.Should().Be(before.ChangedCount + 1, $"{step} must raise one Changed");

    private static void Single(IReadOnlyList<SceneDiagnostic> diagnostics, bool isError, string code, NodeId? node, PropertyId? property)
        => _ = diagnostics.Select(static d => (d.IsError, d.Code, d.Node, d.Property)).Should().Equal(
            [(isError, code, node, property)],
            Describe(diagnostics));

    private static string Describe(SceneEditResult result) => Describe(result.Diagnostics);

    // Messages appear only to explain a failure; no assertion reads them.
    private static string Describe(IReadOnlyList<SceneDiagnostic> diagnostics)
        => diagnostics.Count == 0
            ? "no diagnostics"
            : string.Join("; ", diagnostics.Select(static d => $"[{d.Code}] node {d.Node?.ToString() ?? "-"} property {d.Property?.Qualified() ?? "-"}: {d.Message}"));
}
