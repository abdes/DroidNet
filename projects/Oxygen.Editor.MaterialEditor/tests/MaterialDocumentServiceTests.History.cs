// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Schemas;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Exercises document history and the material editor's real command routing.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Checks exact scalar and multichannel undo/redo with saved-content dirty identity.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task HistoryRestoresExactScalarsAndAllColorChannelsAcrossSave()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/History.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.Roughness, 0.21f);
        edit.Set(MaterialDescriptors.AlphaCutoff, 0.73f);
        edit.Set(MaterialDescriptors.BaseColorR, 0.12f);
        edit.Set(MaterialDescriptors.BaseColorG, 0.34f);
        edit.Set(MaterialDescriptors.BaseColorB, 0.56f);
        edit.Set(MaterialDescriptors.BaseColorA, 0.78f);
        _ = (await service.EditPropertiesAsync(original.DocumentId, edit, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var edited = service.GetDocument(original.DocumentId);
        _ = (await service.SaveAsync(original.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = service.Undo(original.DocumentId).Succeeded.Should().BeTrue();
        var undone = service.GetDocument(original.DocumentId);
        _ = undone.Source.Should().BeEquivalentTo(original.Source);
        _ = undone.IsDirty.Should().BeTrue();
        _ = undone.CookState.Should().Be(MaterialCookState.Stale);
        _ = undone.Revision.Should().Be(2);
        _ = undone.SavedRevision.Should().Be(1);
        _ = service.CanUndo(original.DocumentId).Should().BeFalse();
        _ = service.Redo(original.DocumentId).Succeeded.Should().BeTrue();
        var redone = service.GetDocument(original.DocumentId);
        _ = redone.Source.Should().BeEquivalentTo(edited.Source);
        _ = redone.IsDirty.Should().BeFalse();
        _ = redone.CookState.Should().Be(MaterialCookState.Stale);
        _ = redone.Revision.Should().Be(3);
    }

    /// <summary>One hundred preview samples commit exactly one revision and history entry.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task HundredPreviewSamplesCommitOneUndoEntry()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Gesture.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = service.BeginEditSession(document.DocumentId, "Roughness");
        for (var index = 1; index <= 100; index++)
        {
            _ = (await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, index / 100f), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        }

        _ = service.GetDocument(document.DocumentId).Revision.Should().Be(0);
        _ = service.CompleteEditSession(session, commit: true).Succeeded.Should().BeTrue();
        _ = service.CompleteEditSession(session, commit: true).Succeeded.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Revision.Should().Be(1);
        _ = service.Undo(document.DocumentId).Succeeded.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Source.Should().BeEquivalentTo(document.Source);
        _ = service.CanUndo(document.DocumentId).Should().BeFalse();
        _ = service.Redo(document.DocumentId).Succeeded.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(1f);
    }

    /// <summary>Cancelled, rejected and round-trip no-op gestures preserve history and revision.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CancelRejectedAndNoOpEditsPreserveHistory()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/NoOp.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = service.BeginEditSession(document.DocumentId, "Roughness");
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.9f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.CompleteEditSession(session, commit: false);
        _ = (await service.EditPropertiesAsync(document.DocumentId, PropertyEdit.Single(MaterialDescriptors.Roughness, float.NaN), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeFalse();
        session = service.BeginEditSession(document.DocumentId, "Roughness");
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.9f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.5f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.CompleteEditSession(session, commit: true);

        _ = service.GetDocument(document.DocumentId).Source.Should().BeEquivalentTo(document.Source);
        _ = service.GetDocument(document.DocumentId).Revision.Should().Be(0);
        _ = service.GetDocument(document.DocumentId).IsDirty.Should().BeFalse();
        _ = service.CanUndo(document.DocumentId).Should().BeFalse();
        _ = service.CanRedo(document.DocumentId).Should().BeFalse();
    }

    /// <summary>Closing a gesture and reopening its source retires all old history and callbacks.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CloseAndReopenIsolatesOldSessionAndOtherDocumentHistory()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var first = await service.CreateAsync(new Uri("asset:///Content/Materials/First.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await service.CreateAsync(new Uri("asset:///Content/Materials/Second.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditPropertiesAsync(second.DocumentId, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.3f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = service.BeginEditSession(first.DocumentId, "Roughness");
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.9f), this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(first.DocumentId, discard: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(first.MaterialUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.1f), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeFalse();
        _ = service.CompleteEditSession(session, commit: true);

        _ = reopened.DocumentId.Should().NotBe(first.DocumentId);
        _ = service.CanUndo(reopened.DocumentId).Should().BeFalse();
        _ = service.GetDocument(reopened.DocumentId).Source.Should().BeEquivalentTo(first.Source);
        _ = service.CanUndo(second.DocumentId).Should().BeTrue();
        _ = service.GetDocument(second.DocumentId).Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.3f);
    }

    /// <summary>Save commits a live gesture before its snapshot and ignores later callbacks from it.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SavingActiveGestureCapturesItOnce()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/SaveGesture.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = service.BeginEditSession(document.DocumentId, "Roughness");
        _ = await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.9f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = (await service.PreviewPropertiesAsync(session, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.1f), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeFalse();
        _ = service.CompleteEditSession(session, commit: true);
        _ = service.GetDocument(document.DocumentId).SavedRevision.Should().Be(1);
        _ = service.GetDocument(document.DocumentId).IsDirty.Should().BeFalse();
        _ = service.Undo(document.DocumentId);
        _ = service.CanUndo(document.DocumentId).Should().BeFalse();
        _ = service.GetDocument(document.DocumentId).Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.5f);
    }

    /// <summary>Real view-model commands route to their authoring document, including batched picker color.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ViewModelHistoryCommandsAndColorSessionsAreDocumentScoped()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var first = await service.CreateAsync(new Uri("asset:///Content/Materials/UiFirst.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await service.CreateAsync(new Uri("asset:///Content/Materials/UiSecond.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var firstEditor = new MaterialEditorViewModel(new MaterialDocumentMetadata(first.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        using var secondEditor = new MaterialEditorViewModel(new MaterialDocumentMetadata(second.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => firstEditor.IsLoaded && secondEditor.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);
        secondEditor.RoughnessFactor = 0.2f;
        firstEditor.BeginEditSession("Base color", NumberBoxEditInteractionKind.PointerDrag);
        for (var index = 1; index <= 100; index++)
        {
            firstEditor.SetBaseColor(Color.FromArgb(100, (byte)index, 30, 40));
        }

        firstEditor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await WaitForMaterialUiAsync(() => firstEditor.UndoCommand.CanExecute(parameter: null), this.TestContext.CancellationToken).ConfigureAwait(false);
        await firstEditor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = firstEditor.BaseColorR.Should().Be(1f);
        _ = firstEditor.BaseColorA.Should().Be(1f);
        _ = firstEditor.IsDirty.Should().BeFalse();
        _ = firstEditor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = secondEditor.RoughnessFactor.Should().Be(0.2f);
        await firstEditor.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = firstEditor.BaseColorR.Should().Be(100f / 255);
        _ = firstEditor.BaseColorG.Should().Be(30f / 255);
        _ = firstEditor.BaseColorB.Should().Be(40f / 255);
        _ = firstEditor.BaseColorA.Should().Be(100f / 255);
        _ = firstEditor.CookStatusText.Should().Be("Unsaved changes");
        firstEditor.Deactivate();
        _ = firstEditor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
        firstEditor.RoughnessFactor = 0.8f;
        firstEditor.Activate();
        await firstEditor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = firstEditor.RoughnessFactor.Should().Be(0.5f);
        _ = firstEditor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    /// <summary>Wheel input commits after idle and switching documents cancels a pending wheel session.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ViewModelWheelIdleAndDeactivationEndExactlyOneSession()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/UiWheel.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = new MaterialEditorViewModel(new MaterialDocumentMetadata(original.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForMaterialUiAsync(() => editor.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);
        for (var index = 1; index <= 10; index++)
        {
            editor.BeginEditSession("Roughness", NumberBoxEditInteractionKind.MouseWheel);
            editor.RoughnessFactor = index / 10f;
            editor.CompleteEditSession(new(NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit));
        }

        await WaitForMaterialUiAsync(() => editor.IsDirty, this.TestContext.CancellationToken).ConfigureAwait(false);
        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = editor.RoughnessFactor.Should().Be(0.5f);
        _ = editor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
        editor.BeginEditSession("Roughness", NumberBoxEditInteractionKind.MouseWheel);
        editor.RoughnessFactor = 0.7f;
        editor.CompleteEditSession(new(NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit));
        editor.Deactivate();
        await Task.Delay(350, this.TestContext.CancellationToken).ConfigureAwait(false);
        editor.Activate();
        _ = editor.RoughnessFactor.Should().Be(0.5f);
        _ = editor.IsDirty.Should().BeFalse();
        _ = editor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = editor.RedoCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>Close preparation flushes focused text and commits its gesture before testing dirty state.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ClosePreparationIncludesFocusedTextInTheSavedMaterial()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Focused.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var committer = new FocusedInputCommitter();
        var metadata = new MaterialDocumentMetadata(original.MaterialUri);
        using var editor = new MaterialEditorViewModel(metadata, service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance, inputCommitter: committer);
        await WaitForMaterialUiAsync(() => editor.IsLoaded, this.TestContext.CancellationToken).ConfigureAwait(false);
        editor.BeginEditSession("Roughness", NumberBoxEditInteractionKind.Text);
        committer.Flush = () => editor.RoughnessFactor = 0.67f;

        await editor.PrepareForCloseAsync().ConfigureAwait(false);

        _ = metadata.IsDirty.Should().BeTrue();
        _ = (await editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
        await editor.CloseAsync(discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(original.MaterialUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.67f);
        _ = service.CanUndo(reopened.DocumentId).Should().BeFalse();
    }

    private static async Task WaitForMaterialUiAsync(Func<bool> condition, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        while (!condition())
        {
            await Task.Delay(10, timeout.Token).ConfigureAwait(false);
        }
    }

    private sealed class FocusedInputCommitter : IDocumentInputCommitter
    {
        public Action? Flush { get; set; }

        public Task CommitAsync(Microsoft.UI.WindowId windowId)
        {
            this.Flush?.Invoke();
            this.Flush = null;
            return Task.CompletedTask;
        }
    }
}
