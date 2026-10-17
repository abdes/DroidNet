// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// The authoring frame's steps (scene-edits.md 3.1, 3.3), driven by a <see cref="ProbeEdit"/> so that only the frame
/// is under test.
/// </summary>
[TestClass]
public sealed class SceneAuthoringTests
{
    [TestMethod]
    public async Task Execute_ValidEdit_AppliesRecordsOneEntryAndRaisesItsChanges()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit { Label = "Probe", Changes = [new SceneChange.NodeRenamed(SampleScene.B)] };

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle().Which.Key.Should().Be("Probe");
        var raised = fixture.Changes.All.Should().ContainSingle().Which;
        _ = raised.Args.Document.Should().Be(fixture.Document);
        SceneAssert.Changes(raised.Changes, new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task Execute_AfterDispose_IsRetiredWithoutValidating()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit();
        fixture.Authoring.Dispose();

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Retired(result);
        _ = probe.Log.Calls.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Execute_MissingTarget_IsRefusedWithoutValidating()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        var probe = new ProbeEdit { Targets = [missing] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        _ = probe.Log.Calls.Should().BeEmpty();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_LockedTarget_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);
        var probe = new ProbeEdit { Targets = [SampleScene.B] };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.B);
        _ = probe.Log.Calls.Should().BeEmpty();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_TargetUnderLockedAncestor_IsRefusedForTheTarget()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit { Targets = [SampleScene.A11] }).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.A11);
    }

    [TestMethod]
    public async Task Execute_TwoBadTargets_ReportsOneErrorEach()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();
        fixture.Interaction!.Lock(SampleScene.B);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit { Targets = [missing, SampleScene.B] }).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(SceneEditOutcome.Refused);
        _ = result.Diagnostics.Select(static d => (d.IsError, d.Code, d.Node)).Should().BeEquivalentTo(
        [
            (true, SceneEditCodes.TargetMissing, (NodeId?)missing),
            (true, SceneEditCodes.TargetLocked, (NodeId?)SampleScene.B),
        ]);
    }

    [TestMethod]
    public async Task Execute_LockedTargetWithoutInteractionPort_Applies()
    {
        // Without the editor's port nothing is locked: a headless client edits every node.
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build(), withInteraction: false).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit { Targets = [SampleScene.B] }).ConfigureAwait(false);

        SceneAssert.Applied(result);
    }

    [TestMethod]
    public async Task Execute_ValidationFails_IsInvalidWithoutApplying()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit
        {
            Targets = [SampleScene.B],
            Validation = SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PropertyOutOfRange, "out of range", SampleScene.B)),
        };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, SampleScene.B);
        _ = probe.Log.Calls.Select(static call => call.Step).Should().Equal(ProbeStep.Validate);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_ValidationWarns_AppliesAndReturnsTheWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit
        {
            Targets = [SampleScene.B],
            Validation = SceneEditValidation.Warned(SceneDiagnostic.Warning(SceneEditCodes.AssetReferenceDeleted, "deleted", SampleScene.B)),
        };

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, SampleScene.B);
        _ = fixture.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task Execute_ApplyThrows_FaultsWithTheExceptionAndRecordsNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var failure = new InvalidOperationException("probe failure");
        var before = fixture.Snapshot();

        var execute = () => fixture.Authoring.ExecuteAsync(new ProbeEdit { Failure = failure });

        _ = (await execute.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false)).Which.Should().BeSameAs(failure);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_ApplyReturnsNoChanges_AppliesWithoutRecordingOrRaising()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit { Changes = [] }).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Execute_EditNotChangingTheDocument_RecordsAndKeepsTheDocumentClean()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit { ChangesDocument = false }).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle().Which.ChangesState.Should().BeFalse();
    }

    [TestMethod]
    public async Task Validate_ValidEdit_ChangesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = ProbeEdit.Renaming(SampleScene.B, "B", "Box");
        var before = fixture.Snapshot();

        var validation = await fixture.Authoring.ValidateAsync(probe).ConfigureAwait(false);

        _ = validation.Should().Be(SceneEditValidation.Valid);
        _ = probe.Log.Calls.Select(static call => call.Step).Should().Equal(ProbeStep.Validate);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Validate_LockedTarget_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);

        var validation = await fixture.Authoring.ValidateAsync(new ProbeEdit { Targets = [SampleScene.B] }).ConfigureAwait(false);

        SceneAssert.Refused(validation, SceneEditCodes.TargetLocked, SampleScene.B);
    }

    [TestMethod]
    public async Task Validate_AfterDispose_IsRefusedAsRetired()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Authoring.Dispose();

        var validation = await fixture.Authoring.ValidateAsync(new ProbeEdit()).ConfigureAwait(false);

        SceneAssert.Refused(validation, SceneEditCodes.Retired);
    }

    [TestMethod]
    public async Task Validate_WarningThenExecuteWithout_ReturnsNoWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var warning = SceneDiagnostic.Warning(SceneEditCodes.AssetReferenceDeleted, "deleted", SampleScene.B);
        _ = await fixture.Authoring.ValidateAsync(new ProbeEdit { Validation = SceneEditValidation.Warned(warning) }).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit()).ConfigureAwait(false);

        SceneAssert.Applied(result);
    }

    [TestMethod]
    public async Task Execute_InvalidThenValid_ReturnsNoStaleWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var warnsThenRejects = new ProbeEdit
        {
            Effect = scope => scope.Warn([SceneDiagnostic.Warning(SceneEditCodes.AssetReferenceDeleted, "deleted", SampleScene.B)]),
            Rejection = SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "empty", SampleScene.B)),
        };
        _ = await fixture.Authoring.ExecuteAsync(warnsThenRejects).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new ProbeEdit()).ConfigureAwait(false);

        SceneAssert.Applied(result);
    }

    [TestMethod]
    public async Task Execute_NullEdit_ThrowsArgumentNull()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var execute = () => fixture.Authoring.ExecuteAsync(null!);

        _ = await execute.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Validate_NullEdit_ThrowsArgumentNull()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var validate = () => fixture.Authoring.ValidateAsync(null!);

        _ = await validate.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Dispose_Twice_DoesNotThrow()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Authoring.Dispose();

        var dispose = fixture.Authoring.Dispose;

        _ = dispose.Should().NotThrow();
        _ = fixture.Authoring.IsRetired.Should().BeTrue();
    }

    [TestMethod]
    public async Task Decode_ExposesTheDecodedSceneAndTheContextId()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = fixture.Authoring.Document.Should().Be(fixture.Document);
        _ = fixture.Authoring.Scene.Should().BeSameAs(fixture.Content.Scene);
        _ = fixture.Authoring.Scene.AllNodes.Select(static node => node.Id).Should().Equal(SampleScene.AllNodes);
    }
}
