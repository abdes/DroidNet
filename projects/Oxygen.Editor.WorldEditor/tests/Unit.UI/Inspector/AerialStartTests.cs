// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class AerialStartTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Displays a saved invalid value, rejects further invalid edits, and accepts the user's repair.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task AerialStartRejectsNegativeEditsAndAcceptsOneHundred() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        fixture.Scene.Hydrate(fixture.Scene.Dehydrate() with { Environment = new SceneEnvironmentData { SkyAtmosphere = new SkyAtmosphereEnvironmentData { AerialPerspectiveStartDepthMeters = -1 }, }, });
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView
        {
            ViewModel = model,
        };
        await LoadTestContentAsync(new ScrollViewer { Content = view }).ConfigureAwait(true);
        model.RequestFieldFocus(SceneEnvironmentConstraints.AerialStartPropertyPath);
        await WaitForRenderAsync().ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "AerialPerspectiveStartDepthMeters"))!;
        _ = number.NumberValue.Should().Be(-1);
        _ = model.SkyAtmosphere.AerialPerspectiveStartDepthMetersDiagnostic.Message.Should().Contain("0 m");
        await EnterTextAsync(number, "-2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(-1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await EnterTextAsync(number, "100").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = model.SkyAtmosphere.AerialPerspectiveStartDepthMetersDiagnostic.Message.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });
}
