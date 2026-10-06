// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class AcceptedExposurePresentationTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow("AutoExposureMeteringMask", "Metering mask")]
    [DataRow("AutoExposureTransitionDistanceEv", "Adaptation transition distance")]
    [DataRow("AutoExposureBlackInfluence", "Dark-sample influence")]
    [DataRow("AutoExposureCompensationCurve", "Exposure-compensation curve")]
    public Task AcceptedExposureFieldsUseCleanLabels(string key, string label) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        var view = new EnvironmentView { ViewModel = model, Width = 480, Height = 780 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = key;
        await WaitForRenderAsync().ConfigureAwait(true);
        var field = (FrameworkElement)FindInspectorElement(view, key + "Card");
        var actualLabel = field switch
        {
            InspectorNumberField number => number.Label,
            PropertyCard card => card.PropertyName,
            _ => throw new InvalidOperationException($"Unexpected field type {field.GetType().Name}."),
        };
        _ = actualLabel.Should().Be(label);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });
}
