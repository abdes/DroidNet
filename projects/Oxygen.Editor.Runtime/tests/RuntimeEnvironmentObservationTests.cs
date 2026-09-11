// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class RuntimeEnvironmentObservationTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow("Scene")]
    [DataRow("Run")]
    [DataRow("Cancel")]
    public async Task PendingObservationDoesNotDeliverValuesAfterItsLifetimeEnds(string ending)
    {
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var completion = new TaskCompletionSource<RuntimeEnvironmentState>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = native.Setup(value => value.ObserveEnvironmentAsync()).Returns(completion.Task);
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Environment", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var pending = sut.ObserveEnvironmentAsync(Guid.NewGuid(), target, cancellation.Token);
        switch (ending)
        {
            case "Scene":
                sut.InvalidateScene(target);
                break;
            case "Run":
                _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
                break;
            case "Cancel":
                await cancellation.CancelAsync().ConfigureAwait(false);
                break;
        }

        var result = await pending.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        completion.SetResult(new RuntimeEnvironmentState { Exists = true, PlanetRadiusMeters = 7_000_000 });
        _ = result.Outcome.Succeeded.Should().BeFalse();
        _ = result.Outcome.SceneTarget.Should().Be(target);
        _ = result.State.Should().BeNull();
        sut.EndRun();
    }

    [TestMethod]
    public async Task TransportFailureRetainsItsCauseAndAllowsANewObservation()
    {
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var failure = new TimeoutException("Native observation failed");
        _ = native.Setup(value => value.ObserveEnvironmentAsync()).Throws(failure);
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var target = new RuntimeSceneTarget(sut.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Environment", this.TestContext.CancellationToken).ConfigureAwait(false);

        var rejected = await sut.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rejected.Outcome.Status.Should().Be(RuntimeCommandStatus.Failed);
        _ = rejected.Outcome.Exception.Should().BeSameAs(failure);
        _ = rejected.State.Should().BeNull();
        var expected = new RuntimeEnvironmentState { Exists = true, PostProcessExists = true, DisplayGamma = 2.4f };
        _ = native.Setup(value => value.ObserveEnvironmentAsync()).ReturnsAsync(expected);
        var accepted = await sut.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = accepted.Outcome.Succeeded.Should().BeTrue();
        _ = accepted.State.Should().BeSameAs(expected);
        sut.EndRun();
    }
}
