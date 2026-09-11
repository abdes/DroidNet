// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Verifies conflict recovery through the material document's actual UI owner.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Ordinary Save presents recovery, and Save Copy preserves the original dirty document and history.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task MaterialOwnerSaveConflictOffersRecoveryAndCopyPreservesOriginal()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var prompt = new RecordingConflictPrompt();
        var metadata = new MaterialDocumentMetadata(original.MaterialUri);
        using var editor = new MaterialEditorViewModel(metadata, service, conflictPrompt: prompt);
        await WaitForMaterialUiAsync(() => string.Equals(editor.StatusText, "Cook: NotCooked", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        editor.RoughnessFactor = 0.8f;
        await File.AppendAllTextAsync(original.SourcePath, "\n ", this.TestContext.CancellationToken).ConfigureAwait(false);

        await editor.SaveAsync().ConfigureAwait(false);

        _ = editor.HasSaveConflict.Should().BeTrue();
        _ = prompt.Shown.Should().Be(1);
        _ = prompt.Participant.Should().BeSameAs(editor);
        _ = (await editor.SaveCopyAsync("LocalCopy").ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = editor.HasSaveConflict.Should().BeTrue();
        _ = editor.MaterialUriText.Should().Be(original.MaterialUri.ToString());
        var copy = await service.OpenAsync(new Uri("asset:///Content/Materials/LocalCopy.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = copy.MaterialGuid.Should().NotBe(original.MaterialGuid);
        _ = copy.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.8f);
        _ = (await editor.SaveCopyAsync("LocalCopy").ConfigureAwait(false)).Succeeded.Should().BeFalse();
        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = editor.RoughnessFactor.Should().Be(0.5f);
        await editor.CloseAsync(discard: true).ConfigureAwait(false);
    }

    /// <summary>Close recovery stays inline, and only an accepted reload makes the original safe to close.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task MaterialCloseConflictRemainsUnsavedAfterCopyAndClosesAfterReload()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Close.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var prompt = new RecordingConflictPrompt();
        var metadata = new MaterialDocumentMetadata(original.MaterialUri);
        using var editor = new MaterialEditorViewModel(metadata, service, conflictPrompt: prompt);
        await WaitForMaterialUiAsync(() => string.Equals(editor.StatusText, "Cook: NotCooked", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        editor.RoughnessFactor = 0.8f;
        await File.AppendAllTextAsync(original.SourcePath, "\n ", this.TestContext.CancellationToken).ConfigureAwait(false);
        await editor.PrepareForCloseAsync().ConfigureAwait(false);
        var item = new DocumentCloseItem(metadata, editor.SaveForCloseAsync, editor);

        _ = (await item.SaveAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = item.HasConflict.Should().BeTrue();
        _ = prompt.Shown.Should().Be(0, "the close prompt must own inline actions without a nested dialog");
        _ = (await item.SaveCopyAsync("CloseCopy").ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = item.IsSaved.Should().BeFalse();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = (await item.ReloadAsync().ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = item.IsSaved.Should().BeTrue();
        _ = item.HasConflict.Should().BeFalse();
        _ = editor.RoughnessFactor.Should().Be(0.5f);
        editor.ResumeEditing();
        _ = editor.UndoCommand.CanExecute(parameter: null).Should().BeFalse();
        await editor.CloseAsync(discard: false).ConfigureAwait(false);
    }

    /// <summary>Malformed external source cannot replace the authoring model, history or saved baseline.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task MaterialOwnerRejectedReloadRetainsCurrentValuesAndUndo()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Invalid.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var metadata = new MaterialDocumentMetadata(original.MaterialUri);
        using var editor = new MaterialEditorViewModel(metadata, service);
        await WaitForMaterialUiAsync(() => string.Equals(editor.StatusText, "Cook: NotCooked", StringComparison.Ordinal), this.TestContext.CancellationToken).ConfigureAwait(false);
        editor.RoughnessFactor = 0.8f;
        await File.WriteAllTextAsync(original.SourcePath, "{ invalid", this.TestContext.CancellationToken).ConfigureAwait(false);
        await editor.SaveAsync().ConfigureAwait(false);

        _ = (await editor.ReloadFromDiskAsync().ConfigureAwait(false)).Succeeded.Should().BeFalse();
        _ = editor.RoughnessFactor.Should().Be(0.8f);
        _ = editor.HasSaveConflict.Should().BeTrue();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = editor.UndoCommand.CanExecute(parameter: null).Should().BeTrue();
        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = editor.RoughnessFactor.Should().Be(0.5f);
        await editor.CloseAsync(discard: true).ConfigureAwait(false);
    }

    private sealed class RecordingConflictPrompt : IDocumentConflictPrompt
    {
        public int Shown { get; private set; }

        public IDocumentConflictParticipant? Participant { get; private set; }

        public Task ShowAsync(WindowId windowId, IDocumentMetadata metadata, IDocumentConflictParticipant participant)
        {
            this.Shown++;
            this.Participant = participant;
            return Task.CompletedTask;
        }
    }
}
