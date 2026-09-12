// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks compatibility failures and cancellation before native session creation.</summary>
[TestClass]
public sealed class EngineCompatibilityTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Compatibility failure is visible through lifecycle state and structured operation results.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FailedCompatibilityKeepsNativeUnavailableAndPublishesArtifactDetails()
    {
        var diagnostic = new DiagnosticRecord { OperationId = Guid.NewGuid(), Domain = FailureDomain.RuntimeDiscovery, Severity = DiagnosticSeverity.Error, Code = NativeCompatibilityDiagnosticCodes.ArtifactMismatch, Message = "Runtime library differs", AffectedPath = "runtime.dll" };
        var compatibility = new Mock<INativeCompatibilityService>();
        _ = compatibility.Setup(value => value.VerifyAsync(It.IsAny<Guid>(), It.IsAny<CancellationToken>())).ReturnsAsync(new NativeCompatibilityResult(Artifacts: null, [diagnostic]));
        var published = new List<OperationResult>();
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(published.Add);
        var engine = new EngineService(new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! }, publisher.Object, nativeCompatibility: compatibility.Object);
        await using var lifetime = engine.ConfigureAwait(false);

        Func<Task> initialize = () => engine.InitializeAsync(this.TestContext.CancellationToken).AsTask();
        _ = await initialize.Should().ThrowAsync<NativeCompatibilityException>().ConfigureAwait(false);

        _ = engine.State.Should().Be(EngineServiceState.Faulted);
        _ = engine.WorldCommands.RunId.Should().BeEmpty();
        _ = published.Should().ContainSingle().Which.Diagnostics.Should().Equal(diagnostic);
        await engine.ShutdownAsync().ConfigureAwait(false);
        _ = engine.State.Should().Be(EngineServiceState.NoEngine);
    }

    /// <summary>Cancelling compatibility cannot create a native session afterward.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelledCompatibilityLeavesNoEngine()
    {
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var compatibility = new Mock<INativeCompatibilityService>();
        _ = compatibility.Setup(value => value.VerifyAsync(It.IsAny<Guid>(), It.IsAny<CancellationToken>())).Returns(async (Guid _, CancellationToken token) =>
        {
            entered.SetResult();
            await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
            throw new InvalidOperationException("Unreachable");
        });
        var engine = new EngineService(new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! }, Mock.Of<IOperationResultPublisher>(), nativeCompatibility: compatibility.Object);
        await using var lifetime = engine.ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        var initialize = engine.InitializeAsync(cancellation.Token).AsTask();
        await entered.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await cancellation.CancelAsync().ConfigureAwait(false);
        Func<Task> cancelled = () => initialize;
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = engine.State.Should().Be(EngineServiceState.NoEngine);
        _ = engine.WorldCommands.RunId.Should().BeEmpty();
    }
}
