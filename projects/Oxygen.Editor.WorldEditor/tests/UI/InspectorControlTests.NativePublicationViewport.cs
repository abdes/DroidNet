// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises viewport teardown after the cooking publication pause.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Leaving a scene after publication removes every native view before its framebuffer is reclaimed.</summary>
    /// <returns>The asynchronous rendered-viewport regression.</returns>
    [TestMethod]
    public Task PublicationPauseThenViewportDestructionKeepsNativeFrameAlive() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(40));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var panel = new SwapChainPanel { Width = 320, Height = 200 };
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        var request = new ViewportSurfaceRequest
        {
            DocumentId = fixture.Context.DocumentId,
            ViewportId = Guid.NewGuid(),
            ViewportIndex = 0,
            IsPrimary = true,
        };
        var surface = await fixture.Runtime.AttachViewportAsync(request, panel, timeout.Token).ConfigureAwait(true);
        await using var surfaceLifetime = surface.ConfigureAwait(true);
        await surface.ResizeAsync(320, 200, timeout.Token).ConfigureAwait(true);
        for (var cycle = 0; cycle < 3; ++cycle)
        {
            var view = await fixture.Runtime.CreateViewAsync(new()
            {
                Name = "Publication viewport",
                Purpose = "Viewport",
                CompositingTarget = request.ViewportId,
                Width = 320,
                Height = 200,
            }).ConfigureAwait(true);
            try
            {
                _ = view.IsValid.Should().BeTrue();
                await ObserveRenderedFramesAsync(fixture, timeout.Token).ConfigureAwait(true);
                await fixture.SuspendCookedContentAsync().WaitAsync(timeout.Token).ConfigureAwait(true);
                await fixture.RefreshCookedRootsAsync().WaitAsync(timeout.Token).ConfigureAwait(true);
                await ObserveRenderedFramesAsync(fixture, timeout.Token).ConfigureAwait(true);
            }
            finally
            {
                _ = await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true);
            }

            await ObserveRenderedFramesAsync(fixture, timeout.Token).ConfigureAwait(true);
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
        }
    });

    private static async Task ObserveRenderedFramesAsync(NativeSceneFixture fixture, CancellationToken cancellationToken)
    {
        var nodeId = fixture.Source.RootNodes.Single().Id;

        // Each observation crosses SceneMutation on a later native frame. Continue
        // beyond GPU deferred-release latency after a viewport is destroyed.
        for (var frame = 0; frame < 8; ++frame)
        {
            _ = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        }
    }
}
