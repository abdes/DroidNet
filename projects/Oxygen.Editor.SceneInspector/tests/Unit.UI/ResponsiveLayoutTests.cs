// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.TestSupport;
using static Oxygen.Editor.Controls.Testing.PropertyEditorProbes;
using static Oxygen.Editor.SceneInspector.TestSupport.InspectorModels;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>The inspector's own layout contract: its views place feedback below the rows they compose.</summary>
/// <remarks>
///     The responsive layout of the property rows, fields and sections is tested with the library controls in
///     <c>Oxygen.Editor.Controls</c>.
/// </remarks>
[TestClass]
internal sealed partial class ResponsiveLayoutTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task TransformAxisErrorsSitBelowTheirRowAndNeverMakeItStack() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (TransformViewModel)CreateModel("Transform", fixture);
        var view = new TransformView { ViewModel = model, Width = 760 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var scale = view.FindDescendants().OfType<PropertyCard>().Single(card => string.Equals(card.PropertyName, "Scale", StringComparison.Ordinal));
        var layout = scale.ActualLayout;
        var number = scale.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;

        await EnterTextAsync(number, "0").ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = model.ScaleXDiagnostic.HasError.Should().BeTrue();
        _ = scale.ActualLayout.Should().Be(layout);
        var error = view.FindDescendants().OfType<TextBlock>().Single(text => text.Visibility == Visibility.Visible && AutomationProperties.GetLiveSetting(text) == Microsoft.UI.Xaml.Automation.Peers.AutomationLiveSetting.Polite);
        _ = error.FindAscendant<PropertyCard>().Should().BeNull();
        _ = error.TransformToVisual(view).TransformPoint(default).Y.Should().BeGreaterThanOrEqualTo(
            scale.TransformToVisual(view).TransformPoint(default).Y + scale.ActualHeight);
        number.CancelEdit();
    });
}
