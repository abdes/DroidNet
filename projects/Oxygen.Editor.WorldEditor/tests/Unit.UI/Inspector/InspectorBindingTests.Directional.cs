// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;
using PropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;
using PropertyCard = Oxygen.Editor.Controls.PropertyCard;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

public sealed partial class InspectorBindingTests
{
    [TestMethod]
    public Task DirectionalColorSwatchConvertsLinearChannelsWithoutReauthoringOnLoad() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        var authored = new Vector3(0.2158605f, 0.05126946f, 0.01444384f);
        light.Color = authored;
        using var inspector = fixture.CreateInspectorHost("Light");
        var model = inspector.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        model.IsExpanded = true;
        var view = new DirectionalLightView { ViewModel = model, Width = 340 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var display = Windows.UI.Color.FromArgb(255, 128, 64, 32);
        var section = view.FindDescendant<PropertiesExpander>()!;
        var field = (Oxygen.Editor.Controls.InspectorColorField)view.FindName("ColorField");
        _ = section.BringItemIntoView(field);
        await WaitForRenderAsync().ConfigureAwait(true);
        var swatch = SwatchOf(field);
        _ = model.ColorValue.Should().Be(authored);
        _ = SwatchColor(field).Should().Be(display);
        _ = light.Color.Should().Be(authored);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = fixture.Document.IsDirty.Should().BeFalse();

        await PickDisplayColorAsync(swatch, Windows.UI.Color.FromArgb(255, 64, 128, 32)).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = light.Color.X.Should().BeApproximately(authored.Y, 0.000001f);
        _ = light.Color.Y.Should().BeApproximately(authored.X, 0.000001f);
        _ = light.Color.Z.Should().BeApproximately(authored.Z, 0.000001f);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        await fixture.Document.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = light.Color.Should().Be(authored);
        _ = model.ColorValue.Should().Be(authored);
        _ = SwatchColor(field).Should().Be(display);
    });

    [TestMethod]
    [DataRow("Color")]
    [DataRow("DiskScale")]
    public Task DirectionalRgbVectorsKeepUndoRedoAndCancelTransactions(string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var inspector = fixture.CreateInspectorHost("Light");
        var model = inspector.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        model.IsExpanded = true;
        var view = new DirectionalLightView { ViewModel = model };
        var scroller = new ScrollViewer { Width = 340, Height = 500, Content = view };
        using var nativeHost = new ScaledXamlHost();
        await nativeHost.LoadAsync(scroller, 1, this.TestContext.CancellationToken).ConfigureAwait(true);
        var section = view.FindDescendant<PropertiesExpander>()!;

        // Resolve the declared instance from the expander's Items and bring it into view before
        // walking its card.
        var item = section.Items.OfType<Oxygen.Editor.Controls.InspectorColorField>()
            .Single(candidate => string.Equals(
                candidate.Name,
                string.Equals(field, "Color", StringComparison.Ordinal) ? "ColorField" : "DiskScaleField",
                StringComparison.Ordinal));
        _ = section.BringItemIntoView(item).Should().BeTrue("the declared field must realize in its expander");
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var vector = CardOf(item).FindDescendant<VectorBox>()!;
        var red = (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => vector.FindDescendant<NumberBox>(input => string.Equals(input.Name, "PartNumberBoxX", StringComparison.Ordinal)),
            $"{field}.R",
            this.TestContext.CancellationToken).ConfigureAwait(true);
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        Vector3 Read() => string.Equals(field, "Color", StringComparison.Ordinal) ? light.Color : light.AtmosphereDiskLuminanceScaleRgb;
        var original = Read();

        await EnterTextAsync(red, "0.25").ConfigureAwait(true);
        red.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = Read().Should().Be(new Vector3(0.25f, original.Y, original.Z));
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        await fixture.Document.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = Read().Should().Be(original);
        _ = red.NumberValue.Should().Be(original.X);
        await fixture.Document.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = Read().X.Should().Be(0.25f);
        _ = red.NumberValue.Should().Be(0.25f);

        await EnterTextAsync(red, "0.75").ConfigureAwait(true);
        red.CancelEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = Read().X.Should().Be(0.25f);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    });
}
