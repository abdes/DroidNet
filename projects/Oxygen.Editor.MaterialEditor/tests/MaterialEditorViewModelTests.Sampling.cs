// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Tests the editor's texture sampling rows, occlusion mode and procedural grid section.</summary>
public sealed partial class MaterialEditorViewModelTests
{
    private static readonly AssetPath Bark = AssetPath.Parse("/Content/Textures/Bark.otex");

    /// <summary>Only an assigned slot shows sampling, and its summary names only what differs from the default.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASlotSummarizesWhatDiffersFromTheDefault()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        var slot = editor.TextureChannels[0];
        _ = slot.SamplingVisibility.Should().Be(Visibility.Collapsed);

        slot.SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);
        _ = slot.SamplingVisibility.Should().Be(Visibility.Visible);
        _ = slot.SamplingSummary.Should().Be("Default");

        slot.ScaleU = 4.0;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);
        slot.AddressUIndex = (int)MaterialTextureAddress.Clamp;
        await WaitForEditsAsync(document, entries: 3).ConfigureAwait(false);

        _ = slot.SamplingSummary.Should().Be("Scale 4×, 1×  ·  Clamp U");
        _ = slot.ScaleMark.Should().Be(OverrideState.Full);
        _ = slot.OffsetMark.Should().Be(OverrideState.None);
    }

    /// <summary>Rotation shows degrees and the file holds radians.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RotationIsDegreesInTheEditorAndRadiansInTheFile()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        var slot = editor.TextureChannels[0];
        slot.SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        slot.RotationDegrees = 90.0;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);

        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["base_color"].UvTransform.RotationRadians.Should().BeApproximately(MathF.PI / 2.0f, 0.0001f);
    }

    /// <summary>A row's reset of two fields is one history entry, and an empty selection edits nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ResettingARowIsOneEntry()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        var slot = editor.TextureChannels[0];
        slot.SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);
        slot.ScaleU = 4.0;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);
        slot.ScaleV = 2.0;
        await WaitForEditsAsync(document, entries: 3).ConfigureAwait(false);

        slot.ResetScaleCommand.Execute(parameter: null);
        await WaitForEditsAsync(document, entries: 4).ConfigureAwait(false);

        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["base_color"].Should().Be(new MaterialTextureBinding(Bark));

        slot.AddressUIndex = -1;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = document.History.UndoStack.Should().HaveCount(4);
    }

    /// <summary>Apply to all needs another assigned slot, and reaches it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ApplyToAllReachesTheOtherAssignedSlots()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        var (first, second) = (editor.TextureChannels[0], editor.TextureChannels[1]);
        first.SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);
        _ = first.ApplyToAllCommand.CanExecute(parameter: null).Should().BeFalse();

        second.SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);
        first.ScaleU = 4.0;
        await WaitForEditsAsync(document, entries: 3).ConfigureAwait(false);
        _ = first.ApplyToAllCommand.CanExecute(parameter: null).Should().BeTrue();

        first.ApplyToAllCommand.Execute(parameter: null);
        await WaitForEditsAsync(document, entries: 4).ConfigureAwait(false);

        _ = second.ScaleU.Should().Be(4.0);
    }

    /// <summary>Factor shows a multiplier and Strength does not; Strength with no texture cautions.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheOcclusionModeDecidesItsUnitAndCaution()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        _ = editor.OcclusionUnit.Should().Be("×");
        _ = editor.OcclusionCautionVisibility.Should().Be(Visibility.Collapsed);

        editor.OcclusionModeIndex = (int)MaterialAmbientOcclusionMode.Strength;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        _ = editor.OcclusionUnit.Should().BeEmpty();
        _ = editor.OcclusionCautionVisibility.Should().Be(Visibility.Visible);

        editor.TextureChannels.Single(static channel => channel.Channel == "ambient_occlusion").SelectedTexture = Bark;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);

        _ = editor.OcclusionCautionVisibility.Should().Be(Visibility.Collapsed);
    }

    /// <summary>The toggle adds the grid at the engine's defaults, and undoing removes it again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheGridToggleAddsAndUndoRemoves()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        _ = editor.Grid.ValuesVisibility.Should().Be(Visibility.Collapsed);

        editor.Grid.IsShown = true;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        _ = MaterialDocuments.ContentOf(document).Material.Source.ProceduralGrid.Should().Be(new MaterialProceduralGrid());
        _ = editor.Grid.ValuesVisibility.Should().Be(Visibility.Visible);
        _ = editor.Grid.LineThickness.Should().BeApproximately(0.02, 0.0001);
        _ = editor.Grid.ThicknessMark.Should().Be(OverrideState.None);

        await editor.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = editor.Grid.IsShown.Should().BeFalse();
    }

    /// <summary>A fade's end moved before its start carries the start along, as one edit, so it is never refused.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingOneFadeEndPastTheOtherCarriesIt()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var editor = materials.EditorOf(document);
        editor.Grid.IsShown = true;
        await WaitForEditsAsync(document, entries: 1).ConfigureAwait(false);

        editor.Grid.FadeStart = 20.0;
        await WaitForEditsAsync(document, entries: 2).ConfigureAwait(false);

        var grid = MaterialDocuments.ContentOf(document).Material.Source.ProceduralGrid!;
        _ = (grid.FadeStart, grid.FadeEnd).Should().Be((20.0f, 20.0f));
        _ = editor.Grid.FadeEnd.Should().Be(20.0);
    }
}
