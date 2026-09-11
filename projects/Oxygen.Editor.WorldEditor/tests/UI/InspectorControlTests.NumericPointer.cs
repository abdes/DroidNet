// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Delivers numeric drag and Escape through Windows input.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Escape cancels a captured numeric drag and release cannot commit it afterward.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericPointerEscapeRestoresTheOriginalWithoutHistory(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        var valueText = number.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartValueTextBlock", StringComparison.Ordinal))!;
        var original = number.NumberValue;
        await PointerInput.WaitForTargetAsync(valueText, this.TestContext.CancellationToken).ConfigureAwait(true);
        using var pointer = PointerInput.Capture();
        await PointerInput.MoveAsync(valueText, 0.5, 0.5).ConfigureAwait(true);
        await PointerInput.ButtonAsync(down: true).ConfigureAwait(true);
        try
        {
            _ = valueText.PointerCaptures.Should().NotBeEmpty("the value label must capture the drag");
            await PointerInput.MoveAsync(valueText, 0.7, 0.5).ConfigureAwait(true);
            await PendingNumericEdits(model).ConfigureAwait(true);
            _ = number.NumberValue.Should().NotBe(original, "the drag must produce a real preview");
            await PointerInput.KeyAsync(0x1B).ConfigureAwait(true);
            await PendingNumericEdits(model).ConfigureAwait(true);
            _ = number.NumberValue.Should().Be(original, "Escape must restore the captured before-value");
            _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        }
        finally
        {
            await PointerInput.ButtonAsync(down: false).ConfigureAwait(true);
        }

        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });
}
