// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>
/// Tests the material editor over its document: edits, gestures, history, saves and closes.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed partial class MaterialEditorViewModelTests
{
    /// <summary>Gets or sets cancellation for asynchronous regression tests.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A control's value edits the material, and the editor and its document show the material dirty until it is saved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AControlEditsTheMaterialUntilItIsSaved()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);

        editor.MetallicFactor = 0.25;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.25f);
        _ = editor.IsDirty.Should().BeTrue();

        await editor.SaveAsync().ConfigureAwait(false);

        _ = editor.IsDirty.Should().BeFalse();
        _ = document.IsDirty.Should().BeFalse();
        _ = editor.StatusText.Should().BeEmpty();
    }

    /// <summary>Picking a base color is one edit of its four channels.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ABaseColorIsOneEditOfItsChannels()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);

        editor.SetBaseColor(new ColorChannels(0.5, 0.25, 0.125, 1.0));
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        var pbr = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness;
        _ = (pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB).Should().Be((0.5f, 0.25f, 0.125f));
        _ = document.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>Undo and Redo restore the controls' values, and a gesture is undone as a whole.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoRestoresTheControlsAndAGestureIsUndoneWhole()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);

        editor.BeginEditSession("Roughness", NumberBoxEditInteractionKind.PointerDrag);
        editor.RoughnessFactor = 0.6;
        editor.RoughnessFactor = 0.7;
        editor.RoughnessFactor = 0.8;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = editor.RoughnessFactor.Should().Be(0.5);
        _ = document.IsDirty.Should().BeFalse();

        await editor.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = editor.RoughnessFactor.Should().BeApproximately(0.8, 0.0001);
    }

    /// <summary>Closing commits the focused control's text first, so the saved material holds it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ClosePreparationCommitsTheFocusedControl()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        MaterialEditorViewModel? editor = null;
        var committer = new Mock<IDocumentInputCommitter>();
        _ = committer.Setup(value => value.CommitAsync(It.IsAny<Microsoft.UI.WindowId>()))
            .Returns(() =>
            {
                editor!.MetallicFactor = 0.4;
                return Task.CompletedTask;
            });
        editor = materials.EditorOf(document, inputCommitter: committer.Object);
        using var owned = editor;

        await editor.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();

        var saved = MaterialSourceReader.Read(await File.ReadAllBytesAsync(materials.PathOf("Wood"), this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = saved.PbrMetallicRoughness.MetallicFactor.Should().Be(0.4f);
    }

    /// <summary>A reload shows the file's values and leaves no conflict.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AReloadShowsTheFilesValues()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        editor.MetallicFactor = 0.9;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        var result = await editor.ReloadFromDiskAsync().ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = editor.MetallicFactor.Should().Be(0.0);
        _ = editor.IsDirty.Should().BeFalse();
        _ = editor.HasSaveConflict.Should().BeFalse();
    }

    /// <summary>When its document follows a relocation, the editor shows the material's new name, asset and textures.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheEditorFollowsItsDocumentThroughARelocation()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", AssetPath.Parse("/Content/Textures/Bark.otex"));
        _ = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        File.Move(materials.PathOf("Wood"), materials.PathOf("Oak"));

        await document.FollowRelocationAsync(
            new DocumentRelocation(materials.FileOf("Oak"), document.Version, static texture => AssetPath.Parse(texture.VirtualPath.Replace("/Bark.", "/Oak.", StringComparison.Ordinal))),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = editor.DisplayName.Should().Be("Oak");
        _ = editor.MaterialUriText.Should().Be(materials.FileOf("Oak").Asset.ToUriString());
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureReferences["base_color"].Should().Be(AssetPath.Parse("/Content/Textures/Oak.otex"));
        _ = editor.IsDirty.Should().BeFalse();
    }

    /// <summary>Disposing the editor neither closes nor discards its document.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DisposalLeavesTheDocumentOpen()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var editor = materials.EditorOf(document);
        editor.MetallicFactor = 0.3;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        editor.Dispose();

        _ = materials.Documents.Documents.Should().ContainSingle().Which.Should().BeSameAs(document);
        _ = document.IsDirty.Should().BeTrue();
    }

    private static async Task WaitForEditsAsync(Document document, int entries)
    {
        for (var attempt = 0; attempt < 200 && document.History.UndoStack.Count < entries; attempt++)
        {
            await Task.Delay(10).ConfigureAwait(false);
        }

        _ = document.History.UndoStack.Count.Should().Be(entries);
    }
}
