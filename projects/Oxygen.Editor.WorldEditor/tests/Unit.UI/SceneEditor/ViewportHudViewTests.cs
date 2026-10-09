// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.SceneEditor;

/// <summary>
/// The viewport HUD loads and realizes every flyout: camera, view mode, Show and layout, and the
/// narrow pane folds Show and Layout into Viewport settings. No engine view is created.
/// </summary>
[TestClass]
internal sealed class ViewportHudViewTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Each HUD flyout opens and shows the pane's choices.</summary>
    /// <returns>The asynchronous HUD regression.</returns>
    [TestMethod]
    public Task HudFlyoutsShowThePaneChoices() => EnqueueAsync(async () =>
    {
        using var model = CreateViewModel();
        var view = new Viewport { ViewModel = model, Width = 900, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var dropDowns = view.FindDescendants().OfType<DropDownButton>().ToList();
        _ = dropDowns.Select(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(button))
            .Should().Equal("View camera", "View mode", "Show", "Move snap increment", "Rotate snap increment", "Scale snap increment");

        // Camera: three perspective modes and six orthographic directions.
        await OpenAsync(dropDowns[0], content => _ = Texts(content).Should().Contain(["Turntable", "Trackball", "Fly", "Top", "Back", "Right", "Clipping"])).ConfigureAwait(true);

        // View mode: every mode, in its group.
        await OpenAsync(dropDowns[1], content => _ = Texts(content).Should().Contain(["Lit", "Lighting", "Buffer visualization", "Shadow mask"])).ConfigureAwait(true);

        // Show: the three overlays, bound to the pane.
        await OpenAsync(dropDowns[2], content =>
        {
            var checks = content.FindDescendants().OfType<CheckBox>().ToList();
            _ = checks.Select(check => check.Content).Should().Equal("Grid", "Selection outline", "Camera preview", "Statistics");
            checks[3].IsChecked = true;
        }).ConfigureAwait(true);
        _ = model.ShowStatistics.Should().BeTrue();

        // Layout: one pictogram per layout.
        await OpenAsync(Named<Button>(view, "LayoutButton"), content =>
            _ = content.FindDescendants().OfType<ViewportLayoutPictogram>().Should().HaveCount(Enum.GetValues<SceneViewLayout>().Length)).ConfigureAwait(true);
    });

    /// <summary>A narrow pane drops labels and folds Show and Layout into Viewport settings.</summary>
    /// <returns>The asynchronous density regression.</returns>
    [TestMethod]
    public Task NarrowPaneFoldsShowAndLayoutIntoSettings() => EnqueueAsync(async () =>
    {
        using var model = CreateViewModel();
        var view = new Viewport { ViewModel = model, Width = 360, Height = 300 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = Named<Button>(view, "ShowButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Named<Button>(view, "LayoutButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Named<Button>(view, "SettingsButton").Visibility.Should().Be(Visibility.Visible);
        _ = Named<TextBlock>(view, "CameraLabel").Visibility.Should().Be(Visibility.Collapsed);
    });

    private static ViewportViewModel CreateViewModel()
    {
        // The pane never gets a surface: attaching is cancelled, so no engine view is created.
        var engine = new Mock<IEngineService>();
        _ = engine
            .Setup(service => service.AttachViewportAsync(It.IsAny<ViewportSurfaceRequest>(), It.IsAny<SwapChainPanel>(), It.IsAny<CancellationToken>()))
            .Returns(ValueTask.FromCanceled<IViewportSurfaceLease>(new CancellationToken(canceled: true)));
        return new ViewportViewModel(
            Guid.NewGuid(),
            engine.Object,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance);
    }

    private static T Named<T>(FrameworkElement root, string name)
        where T : FrameworkElement
        => root.FindDescendants().OfType<T>().Single(element => string.Equals(element.Name, name, StringComparison.Ordinal));

    private static IEnumerable<string> Texts(FrameworkElement root) => root.FindDescendants().OfType<TextBlock>().Select(text => text.Text);

    /// <summary>Opens the button's flyout, inspects its realized content and closes it.</summary>
    private static async Task OpenAsync(Button button, Action<FrameworkElement> inspect)
    {
        var flyout = (Flyout)button.Flyout;
        var opened = new TaskCompletionSource();
        var closed = new TaskCompletionSource();
        flyout.Opened += (_, _) => opened.TrySetResult();
        flyout.Closed += (_, _) => closed.TrySetResult();
        flyout.ShowAt(button);
        await opened.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        try
        {
            inspect((FrameworkElement)flyout.Content);
        }
        finally
        {
            // A flyout shown while another is still closing does not open.
            flyout.Hide();
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        }
    }
}
