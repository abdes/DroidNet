// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class RuntimeBackgroundObservationTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Observation_DropsValuesFromAnEarlierSceneActivation()
    {
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var completion = new TaskCompletionSource<RuntimeBackgroundState>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = native.Setup(value => value.ObserveBackgroundAsync()).Returns(completion.Task);
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var first = new RuntimeSceneTarget(sut.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), first, "First", this.TestContext.CancellationToken).ConfigureAwait(false);
        var pending = sut.ObserveBackgroundAsync(Guid.NewGuid(), first, this.TestContext.CancellationToken);
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), first with { ActivationId = Guid.NewGuid() }, "Second", this.TestContext.CancellationToken).ConfigureAwait(false);

        completion.SetResult(new RuntimeBackgroundState(Exists: true, Vector3.One, AtmosphereEnabled: false));
        var result = await pending.ConfigureAwait(false);

        _ = result.Outcome.Status.Should().Be(RuntimeCommandStatus.Rejected);
        _ = result.Outcome.SceneTarget.Should().Be(first);
        _ = result.State.Should().BeNull();
    }
}
