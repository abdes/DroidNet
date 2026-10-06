// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldControls;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.SceneEditor;

[TestClass]
public sealed partial class CapturedSkyTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Radiance edits, history, reopen and view replacement reach the scene's rendered IBL products.</summary>
    /// <returns>The asynchronous native viewport workflow.</returns>
    [TestMethod]
    public Task CapturedSkyInspectorHistoryReopenAndViewRecreationUseCurrentProducts() => EnqueueAsync(async () =>
    {
        await this.RunIblInspectorWorkflowAsync(Microsoft.Extensions.Logging.Abstractions.NullLoggerFactory.Instance).ConfigureAwait(true);
    });

    private static (Grid host, EnvironmentView inspector, ScrollViewer scroller, SwapChainPanel panel) CreateIblHost(EnvironmentViewModel model)
    {
        var inspector = new EnvironmentView
        {
            ViewModel = model,
        };
        var scroller = new ScrollViewer
        {
            Content = inspector,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        var panel = new SwapChainPanel
        {
            Width = 320,
            Height = 240,
        };
        var host = new Grid();
        host.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        host.ColumnDefinitions.Add(new() { Width = new GridLength(320) });
        Grid.SetColumn(panel, 1);
        host.Children.Add(scroller);
        host.Children.Add(panel);
        return (host, inspector, scroller, panel);
    }

    private static bool IsNewSkyRender(RuntimeEnvironmentState state, RuntimeEnvironmentState previous) => state.SkyLightFrameSequence > previous.SkyLightFrameSequence && state.SkyLightPublishedRevision != previous.SkyLightPublishedRevision;

    private async Task RunIblInspectorWorkflowAsync(ILoggerFactory loggerFactory)
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedShadowTransitionScene(scene, 1), loggerFactory: loggerFactory);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var previousSize = window.AppWindow.Size;
        using var restoreSize = Disposable.Create(() => window.AppWindow.Resize(previousSize));
        var (host, inspector, scroller, panel) = CreateIblHost(fixture.Model);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var scale = host.XamlRoot.RasterizationScale;
        window.AppWindow.Resize(new((int)Math.Ceiling(960 * scale), (int)Math.Ceiling(600 * scale)));
        window.Activate();
        var (baseline, recreated) = await this.VerifyIblInspectorViewportAsync(fixture, inspector, scroller, panel, host, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        var reopened = await this.ObserveReplacementIblViewportAsync(fixture, panel, recreated, "reopened", timeout.Token).ConfigureAwait(true);
        _ = reopened.MieAnisotropy.Should().Be((float)NativeEnvironmentFields.Single(value => string.Equals(value.Field, "MieAnisotropy", StringComparison.Ordinal)).ExpectedValue);
        _ = fixture.Source.Environment.SkyAtmosphere.MieAnisotropy.Should().Be(reopened.MieAnisotropy);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await fixture.SwitchToNewSceneAsync(1, timeout.Token).ConfigureAwait(true);
        var replaced = await this.ObserveReplacementIblViewportAsync(fixture, panel, reopened, "scene replaced", timeout.Token).ConfigureAwait(true);
        _ = replaced.MieAnisotropy.Should().Be(baseline.MieAnisotropy);
        _ = replaced.SkyLightEmptyCapture.Should().BeFalse();
        _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
    }

    private async Task<(RuntimeEnvironmentState baseline, RuntimeEnvironmentState recreated)> VerifyIblInspectorViewportAsync(NativeSceneFixture fixture, EnvironmentView inspector, ScrollViewer scroller, SwapChainPanel panel, FrameworkElement host, CancellationToken cancellationToken)
    {
        var request = new ViewportSurfaceRequest
        {
            DocumentId = fixture.Context.DocumentId,
            ViewportId = Guid.NewGuid(),
            ViewportIndex = 0,
            IsPrimary = true,
        };
        var surface = await fixture.Runtime.AttachViewportAsync(request, panel, cancellationToken).ConfigureAwait(true);
        await using var surfaceLifetime = surface.ConfigureAwait(true);
        await surface.ResizeAsync(320, 240, cancellationToken).ConfigureAwait(true);
        Task<RuntimeViewId> CreateViewAsync() => fixture.Runtime.CreateViewAsync(new() { Name = "Captured sky workflow", Purpose = "Viewport", CompositingTarget = request.ViewportId, Width = 320, Height = 240, });
        var view = await CreateViewAsync().ConfigureAwait(true);
        try
        {
            _ = view.IsValid.Should().BeTrue();
            var (baseline, redone) = await this.VerifyIblHistoryAsync(fixture, inspector, scroller, host, cancellationToken).ConfigureAwait(true);
            _ = (await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true)).Should().BeTrue();
            var beforeRecreation = await fixture.ReadNativeAsync(cancellationToken).ConfigureAwait(true);
            view = await CreateViewAsync().ConfigureAwait(true);
            _ = view.IsValid.Should().BeTrue();
            var recreated = await WaitForCurrentSkyAsync(this.TestContext, "view recreated", fixture, state => state.SkyLightFrameSequence > beforeRecreation.SkyLightFrameSequence, cancellationToken).ConfigureAwait(true);
            _ = recreated.SkyLightSceneLifetime.Should().Be(redone.SkyLightSceneLifetime);
            _ = recreated.SkyLightPublishedRevision.Should().Be(redone.SkyLightPublishedRevision);
            _ = recreated.SkyLightPublishedSourceRevision.Should().Be(redone.SkyLightPublishedSourceRevision);
            return (baseline, recreated);
        }
        finally
        {
            _ = await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true);
        }
    }

    private async Task<RuntimeEnvironmentState> ObserveReplacementIblViewportAsync(NativeSceneFixture fixture, SwapChainPanel panel, RuntimeEnvironmentState previous, string stage, CancellationToken cancellationToken)
    {
        var request = new ViewportSurfaceRequest
        {
            DocumentId = fixture.Context.DocumentId,
            ViewportId = Guid.NewGuid(),
            ViewportIndex = 0,
            IsPrimary = true,
        };
        var surface = await fixture.Runtime.AttachViewportAsync(request, panel, cancellationToken).ConfigureAwait(true);
        await using var surfaceLifetime = surface.ConfigureAwait(true);
        await surface.ResizeAsync(320, 240, cancellationToken).ConfigureAwait(true);
        var view = await fixture.Runtime.CreateViewAsync(new() { Name = "Reopened sky workflow", Purpose = "Viewport", CompositingTarget = request.ViewportId, Width = 320, Height = 240, }).ConfigureAwait(true);
        try
        {
            _ = view.IsValid.Should().BeTrue();
            return await WaitForCurrentSkyAsync(this.TestContext, stage, fixture, state => state.SkyLightSceneLifetime != previous.SkyLightSceneLifetime, cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            _ = await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true);
        }
    }

    private async Task<(RuntimeEnvironmentState baseline, RuntimeEnvironmentState redone)> VerifyIblHistoryAsync(NativeSceneFixture fixture, EnvironmentView inspector, ScrollViewer scroller, FrameworkElement host, CancellationToken cancellationToken)
    {
        var field = NativeEnvironmentFields.Single(value => string.Equals(value.Field, "MieAnisotropy", StringComparison.Ordinal));
        FrameworkElement control;
        try
        {
            control = await FindEnvironmentFieldControlAsync(inspector, scroller, fixture.Model, field, cancellationToken).ConfigureAwait(true);
        }
        catch (InvalidOperationException)
        {
            this.TestContext.WriteLine($"Inspector extent={scroller.ActualWidth}x{scroller.ActualHeight}, viewport={scroller.ViewportHeight}, scroll={scroller.VerticalOffset}/{scroller.ScrollableHeight}");
            throw;
        }

        await fixture.Model.PendingEdits.ConfigureAwait(true);
        var baseline = await WaitForCurrentSkyAsync(this.TestContext, "baseline", fixture, _ => true, cancellationToken).ConfigureAwait(true);
        _ = baseline.SkyLightEmptyCapture.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await SetEnvironmentControlValueAsync(control, field.ControlValue).ConfigureAwait(true);
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        var edited = await WaitForCurrentSkyAsync(this.TestContext, "edited", fixture, state => IsNewSkyRender(state, baseline), cancellationToken).ConfigureAwait(true);
        _ = edited.MieAnisotropy.Should().Be((float)field.ExpectedValue);
        _ = edited.SkyLightPublishedSourceRevision.Should().NotBe(baseline.SkyLightPublishedSourceRevision);
        await fixture.Context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        var undone = await WaitForCurrentSkyAsync(this.TestContext, "undo", fixture, state => IsNewSkyRender(state, edited), cancellationToken).ConfigureAwait(true);
        _ = undone.MieAnisotropy.Should().Be(baseline.MieAnisotropy);
        _ = undone.SkyLightPublishedSourceRevision.Should().Be(baseline.SkyLightPublishedSourceRevision);
        await fixture.Context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        var redone = await WaitForCurrentSkyAsync(this.TestContext, "redo", fixture, state => IsNewSkyRender(state, undone), cancellationToken).ConfigureAwait(true);
        _ = redone.SkyLightPublishedSourceRevision.Should().Be(edited.SkyLightPublishedSourceRevision);
        return (baseline, redone);
    }
}
