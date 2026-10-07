// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.SceneEditor;

[TestClass]
public sealed partial class SceneReplacementTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A scene switch immediately after publication and surface release keeps the native loop alive at a slow native cadence.</summary>
    /// <returns>The asynchronous native scene-transition regression.</returns>
    [TestMethod]
    public Task SceneSwitchImmediatelyAfterPublicationKeepsNativeLoopAlive() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedShadowTransitionScene(scene, 4));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(40));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        fixture.Runtime.TargetFps = 10;
        var panel = new SwapChainPanel
        {
            Width = 640,
            Height = 360,
        };
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(0);
        var request = new ViewportSurfaceRequest
        {
            DocumentId = fixture.Context.DocumentId,
            ViewportId = Guid.NewGuid(),
            ViewportIndex = 0,
            IsPrimary = true,
        };
        var surface = await fixture.Runtime.AttachViewportAsync(request, panel, timeout.Token).ConfigureAwait(true);
        _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(1);
        await using (surface.ConfigureAwait(true))
        {
            await surface.ResizeAsync(3213, 1271, timeout.Token).ConfigureAwait(true);
            var view = await fixture.Runtime.CreateViewAsync(new() { Name = "Scene switch", Purpose = "Viewport", CompositingTarget = request.ViewportId, Width = 3213, Height = 1271, }).ConfigureAwait(true);
            try
            {
                _ = view.IsValid.Should().BeTrue();
                _ = await fixture.Runtime.SetViewCameraControlModeAsync(view, CameraControlMode.OrbitTurntable).ConfigureAwait(true);
                _ = await fixture.Runtime.SetViewCameraSettingsAsync(view, 90, 0.1f, 1000).ConfigureAwait(true);
                await ObserveRenderedFramesAsync(fixture, timeout.Token).ConfigureAwait(true);
                await fixture.SuspendCookedContentAsync().WaitAsync(timeout.Token).ConfigureAwait(true);
                await fixture.RefreshCookedRootsAsync(mountPublished: false).WaitAsync(timeout.Token).ConfigureAwait(true);
            }
            finally
            {
                _ = (await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true)).Should().BeTrue();
            }
        }

        _ = surface.IsAttached.Should().BeFalse();
        _ = fixture.Runtime.ActiveSurfaceCount.Should().Be(0);
        await fixture.SwitchToNewSceneAsync(1, timeout.Token).ConfigureAwait(true);
        _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);

        await ObserveRenderedFramesAsync(fixture, timeout.Token).ConfigureAwait(true);
        _ = fixture.Results.Should().NotContain(result => result.OperationKind == RuntimeOperationKinds.Loop);
    });
}
