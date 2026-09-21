// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    public Task AssetFailure_CrossesNativeFacadeWithRequestIdentityAndGeneration()
        => this.RunNativeCommandsAsync(this.CheckNativeAssetFailureAsync);

    [TestMethod]
    public Task StartupReadiness_AllowsImmediateCookedRootUnmount()
        => this.RunNativeCommandsAsync(_ => Task.CompletedTask);

    [SuppressMessage("Reliability", "CA2025:Ensure tasks using IDisposable instances complete before the instances are disposed", Justification = "Finally stops and awaits the native loop and dispatcher cleanup before disposing native owners.")]
    private async Task RunNativeCommandsAsync(Func<RuntimeCommandDispatcher, Task> check, IReadOnlyList<string>? cookedRoots = null)
    {
        var ui = new QueuedContext();
        var previous = SynchronizationContext.Current;
        using var runner = new EngineRunner();
        EngineContext context;
        try
        {
            SynchronizationContext.SetSynchronizationContext(ui);
            context = runner.CreateEngine(new EngineConfig
            {
                TargetFps = 30,
                EnableAssetLoader = true,
                Graphics = new GraphicsConfigManaged { Headless = true },
            });
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }

        using (context)
        {
            var loop = runner.RunEngineAsync(context);
            var cleanup = runner.WaitForLoopCleanupAsync();
            var commands = new RuntimeCommandDispatcher();
            try
            {
                await runner.WaitForEngineReadyAsync().WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
                var transport = new NativeRuntimeCommandTransport(context);
                await transport.ReplaceCookedRootsAsync(cookedRoots ?? []).ConfigureAwait(false);
                _ = commands.BeginRun(transport, loop);
                await check(commands).ConfigureAwait(false);
            }
            finally
            {
                commands.EndRun();
                runner.StopEngine(context);
                await loop.ConfigureAwait(false);
                await ui.Posted.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
                ui.Drain();
                await cleanup.ConfigureAwait(false);
            }
        }
    }

    private async Task CheckNativeAssetFailureAsync(RuntimeCommandDispatcher commands)
    {
        var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        var activated = await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Asset failures", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = activated.Status.Should().Be(RuntimeCommandStatus.Accepted);
        var nodeId = Guid.NewGuid();
        var created = await commands.CreateNodeAsync(
            new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeCreateNode("Cube", nodeId, ParentId: null, InitializeWorldAsRoot: true)),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = created.Status.Should().Be(RuntimeCommandStatus.Accepted);
        var completion = new TaskCompletionSource<RuntimeAssetLoadFailedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        commands.AssetLoadFailed += (_, args) => completion.TrySetResult(args);
        var request = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetGeometry(nodeId, "/Content/DoesNotExist.ogeo"));

        var accepted = commands.Execute(request, this.TestContext.CancellationToken);
        var failure = await completion.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = accepted.Status.Should().Be(RuntimeCommandStatus.Accepted);
        _ = failure.Request.Should().BeSameAs(request);
        _ = failure.Generation.Should().BePositive();
        _ = failure.Message.Should().NotBeNullOrWhiteSpace();
        _ = commands.IsCurrentAssetRequest(failure.Request).Should().BeTrue();
        _ = commands.IsCurrentAssetFailure(failure).Should().BeTrue();
        var replacement = request with { OperationId = Guid.NewGuid(), Command = new RuntimeSetGeometry(nodeId, "asset:///Engine/Generated/BasicShapes/Cube") };
        _ = commands.Execute(replacement, this.TestContext.CancellationToken).Status.Should().Be(RuntimeCommandStatus.Accepted);
        await this.WaitForAppliedAssetAsync(commands, replacement).ConfigureAwait(false);
        _ = commands.IsCurrentAssetFailure(failure).Should().BeFalse();
        var material = new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetMaterialOverride(nodeId, 0, "asset:///Engine/Generated/Materials/Default"));
        _ = commands.Execute(material, this.TestContext.CancellationToken).Status.Should().Be(RuntimeCommandStatus.Accepted);
        await this.WaitForAppliedAssetAsync(commands, material).ConfigureAwait(false);
    }

    private async Task WaitForAppliedAssetAsync(RuntimeCommandDispatcher commands, RuntimeWorldRequest request)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        while (!commands.AssetRequests.Any(status => status.Request == request && status.Succeeded == true))
        {
            await Task.Delay(10, timeout.Token).ConfigureAwait(false);
        }

        _ = commands.AssetRequests.Single(status => status.Request == request).Generation.Should().BePositive();
    }
}
