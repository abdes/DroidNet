// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class ExposureCurveEditorTests
{
    [TestMethod]
    public async Task CurveKeys_AddRemoveAndHistoryUseTheExistingSceneOwner()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        var editor = model.CurveEditor;
        editor.AddKeyCommand.Execute(null);
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Scene.Environment.PostProcess.AutoExposureCompensationCurve.Should().Equal(new ExposureCompensationKeyData(0, 0));
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var key = editor.Keys.Single();
        editor.RemoveKeyCommand.Execute(key);
        await model.PendingEdits.ConfigureAwait(false);
        _ = editor.Keys.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().HaveCount(2);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(false);
        _ = editor.Keys.Should().ContainSingle();
        _ = editor.Curve.Should().Equal(new ExposureCompensationKeyData(0, 0));
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(false);
        _ = editor.Curve.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CurveKeyPolicy_RetainsStableRowsAndRejectsNonFiniteOrUnorderedCoordinates()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        model.AutoExposureCompensationCurve = [new(-2, 0), new(0, 1), new(2, 0)];
        await model.PendingEdits.ConfigureAwait(false);
        var editor = model.CurveEditor;
        var middle = editor.Keys[1];
        _ = editor.Validate(middle, true, -2).Should().BeFalse();
        _ = editor.Validate(middle, true, 2).Should().BeFalse();
        _ = editor.Validate(middle, true, 1).Should().BeTrue();
        _ = editor.Validate(middle, false, float.PositiveInfinity).Should().BeFalse();
        _ = editor.Validate(middle, false, float.NaN).Should().BeFalse();
        model.AutoExposureCompensationCurve = [new(-2, 0), new(1, 0.5f), new(2, 0)];
        await model.PendingEdits.ConfigureAwait(false);
        _ = editor.Keys[1].Should().BeSameAs(middle);
        _ = middle.MeteredEv.Should().Be(1);
        _ = middle.CompensationEv.Should().Be(0.5f);
    }

    [TestMethod]
    public async Task CurveKeyLimit_DisabledInputAndDisposalDoNotSubmitEdits()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        model.AutoExposureCompensationCurve = Enumerable.Range(0, 64).Select(index => new ExposureCompensationKeyData(index, 0)).ToImmutableArray();
        await model.PendingEdits.ConfigureAwait(false);
        var editor = model.CurveEditor;
        fixture.Context.History.Clear();
        editor.AddKeyCommand.Execute(null);
        _ = editor.Keys.Should().HaveCount(64);
        model.SetInputEnabled(false);
        editor.RemoveKeyCommand.Execute(editor.Keys[0]);
        _ = editor.Keys.Should().HaveCount(64);
        editor.Dispose();
        editor.Keys[0].CompensationEv = 2;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Scene.Environment.PostProcess.AutoExposureCompensationCurve[0].CompensationEv.Should().Be(0);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CurveGesture_CancelRestoresTheCapturedCurveWithoutNewHistory()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        model.AutoExposureCompensationCurve = [new(0, 1)];
        await model.PendingEdits.ConfigureAwait(false);
        fixture.Context.History.Clear();
        var editor = model.CurveEditor;
        editor.Begin(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag));
        editor.Keys[0].CompensationEv = 2;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        editor.Complete(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Cancel));
        await model.PendingEdits.ConfigureAwait(false);
        _ = editor.Curve.Should().Equal(new ExposureCompensationKeyData(0, 1));
        _ = editor.Keys[0].CompensationEv.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    }
}
