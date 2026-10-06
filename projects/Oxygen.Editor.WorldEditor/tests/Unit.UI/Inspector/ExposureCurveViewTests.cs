// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class ExposureCurveViewTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task CurveViewMultipleKeysShareHeadersAndStretchPreviewWithAccessibleFields() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        model.Exposure.AutoExposureCompensationCurve = [new(-2, 0), new(0, 1), new(2, 0)];
        await model.PendingEdits.ConfigureAwait(true);
        var view = new ExposureCompensationCurveEditorView { ViewModel = model.Exposure.CurveEditor, Width = 420 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = view.FindDescendants().OfType<TextBlock>().Count(text => string.Equals(text.Text, "Metered EV100", StringComparison.Ordinal)).Should().Be(1);
        _ = view.FindDescendants().OfType<TextBlock>().Count(text => string.Equals(text.Text, "Compensation EV", StringComparison.Ordinal)).Should().Be(1);
        var preview = view.FindDescendant<Polyline>()!;
        _ = preview.Should().NotBeNull();
        _ = preview.Stretch.Should().Be(Stretch.Fill);
        _ = preview.ActualWidth.Should().BeApproximately(view.ActualWidth, 1);
        var fields = view.FindDescendants().OfType<NumberBox>().ToArray();
        _ = fields.Should().HaveCount(6);
        foreach (var field in fields)
        {
            _ = field.LabelPosition.Should().Be(DroidNet.Controls.LabelPosition.None);
            _ = AutomationProperties.GetName(field).Should().Be(Equals(field.Tag, "MeteredEv") ? "Metered EV100" : "Compensation EV");
        }

        view.Width = 600;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = preview.ActualWidth.Should().BeApproximately(view.ActualWidth, 1);
    });

    [TestMethod]
    public Task CurveViewKeyTextValidationAndRemoveKeepTheSceneTransactionOwner() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        model.Exposure.AutoExposureCompensationCurve = [new(-2, 0), new(0, 1), new(2, 0)];
        await model.PendingEdits.ConfigureAwait(true);
        fixture.Context.History.Clear();
        var view = new ExposureCompensationCurveEditorView { ViewModel = model.Exposure.CurveEditor, Width = 420 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var key = model.Exposure.CurveEditor.Keys[1];
        var metered = view.FindDescendant<NumberBox>(number => ReferenceEquals(number.DataContext, key) && Equals(number.Tag, "MeteredEv"))!;
        _ = metered.Should().NotBeNull();
        await EnterTextAsync(metered, "2").ConfigureAwait(true);
        metered.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = key.MeteredEv.Should().Be(0);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await EnterTextAsync(metered, "1").ConfigureAwait(true);
        metered.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = key.MeteredEv.Should().Be(1);
        _ = fixture.Scene.Environment.PostProcess.AutoExposureCompensationCurve[1].MeteredEv.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var remove = view.FindDescendant<Button>(button => ReferenceEquals(button.DataContext, key)
            && Equals(ToolTipService.GetToolTip(button), "Remove curve key"))!;
        ((IInvokeProvider)new ButtonAutomationPeer(remove).GetPattern(PatternInterface.Invoke)).Invoke();
        await model.PendingEdits.ConfigureAwait(true);
        _ = model.Exposure.CurveEditor.Keys.Should().HaveCount(2);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(2);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = model.Exposure.CurveEditor.Keys.Should().HaveCount(3);
    });
}
