// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises Aerial Start validation and navigation through the actual controls.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Displays a saved invalid value, rejects further invalid edits, and accepts the user's repair.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task AerialStartRejectsNegativeEditsAndAcceptsOneHundred() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        fixture.Scene.Hydrate(fixture.Scene.Dehydrate() with
        {
            Environment = new SceneEnvironmentData
            {
                SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = -1 },
            },
        });
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        await LoadTestContentAsync(new ScrollViewer { Content = view }).ConfigureAwait(true);
        model.RequestFieldFocus(SceneEnvironmentConstraints.AerialStartPropertyPath);
        await WaitForRenderAsync().ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "AerialPerspectiveStartDepthMeters"))!;
        _ = number.NumberValue.Should().Be(-1);
        _ = model.AerialPerspectiveStartDepthMetersDiagnostic.Message.Should().Contain("0 m");
        await EnterTextAsync(number, "-2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(-1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await EnterTextAsync(number, "100").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = model.AerialPerspectiveStartDepthMetersDiagnostic.Message.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Diagnostic navigation expands and focuses the actual Aerial Start control.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task EnvironmentDiagnosticNavigationFocusesAerialStart() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        await LoadTestContentAsync(new ScrollViewer { Content = view }).ConfigureAwait(true);
        model.RequestFieldFocus(SceneEnvironmentConstraints.AerialStartPropertyPath);
        await WaitForRenderAsync().ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "AerialPerspectiveStartDepthMeters"))!;
        _ = FocusManager.GetFocusedElement(view.XamlRoot).Should().Be(number);
        _ = model.PendingFieldFocus.Should().BeNull();
    });
}
