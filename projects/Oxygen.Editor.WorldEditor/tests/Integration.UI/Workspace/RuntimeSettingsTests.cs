// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using DryIoc;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class RuntimeSettingsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Accepted settings reach native configuration; a stopped runtime publishes a scoped rejection.</summary>
    /// <param name="fps">Whether to exercise FPS or logging verbosity.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task RuntimeSettingControlsReportNativeRejection(bool fps) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var container = new Container();
        await using var containerLifetime = container.ConfigureAwait(true);
        using var model = await fixture.CreateSceneEditorAsync(container).ConfigureAwait(true);
        var view = new SceneEditorView
        {
            ViewModel = model
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var settings = (ToolBarButton)view.FindName("SettingsButton");
        var flyout = (Flyout)settings.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnOpened(object? sender, object args) => opened.TrySetResult();
        flyout.Opened += OnOpened;
        flyout.ShowAt(settings);
        try
        {
            await opened.Task.WaitAsync(timeout.Token).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
            var slider = flyout.Content.FindDescendant<Slider>()!;
            var number = flyout.Content.FindDescendant<NumberBox>()!;
            if (fps)
            {
                slider.Value = 45;
                _ = fixture.Runtime.TargetFps.Should().Be(45);
            }
            else
            {
                await SetEnvironmentControlValueAsync(number, 2f).ConfigureAwait(true);
                _ = fixture.Runtime.EngineLoggingVerbosity.Should().Be(2);
            }

            fixture.Results.Clear();
            await fixture.Runtime.ShutdownAsync().ConfigureAwait(true);
            if (fps)
            {
                slider.Value = 30;
            }
            else
            {
                await SetEnvironmentControlValueAsync(number, -3f).ConfigureAwait(true);
            }

            AssertRuntimeSettingsFailure(fixture, fps);
        }
        finally
        {
            flyout.Opened -= OnOpened;
            flyout.Hide();
            await view.DeactivateAsync().ConfigureAwait(true);
        }
    });

    private static void AssertRuntimeSettingsFailure(NativeSceneFixture fixture, bool fps)
    {
        var failure = fixture.Results.Last(result => string.Equals(result.OperationKind, RuntimeOperationKinds.SettingsApply, StringComparison.Ordinal));
        _ = failure.Status.Should().Be(OperationStatus.Failed);
        _ = failure.AffectedScope.ProjectId.Should().Be(fixture.Source.Project.ProjectInfo.Id);
        _ = failure.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(DiagnosticCodes.SettingsPrefix + (fps ? "TARGET_FPS_REJECTED" : "LOGGING_VERBOSITY_REJECTED"));
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    }
}
