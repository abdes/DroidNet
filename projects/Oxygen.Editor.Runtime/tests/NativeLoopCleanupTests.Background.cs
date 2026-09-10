// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    [TestMethod]
    public Task BackgroundColor_StoresExactNativeValueAndRejectsNonFiniteInput()
        => this.RunNativeCommandsAsync(this.CheckNativeBackgroundAsync);

    private async Task CheckNativeBackgroundAsync(RuntimeCommandDispatcher commands)
    {
        var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        var activation = await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Background", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = activation.Succeeded.Should().BeTrue();
        var color = new Vector3(0.125f, 0.375f, 0.625f);
        var applied = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetBackgroundColor(color)), this.TestContext.CancellationToken);

        var observed = await commands.ObserveBackgroundAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = applied.Succeeded.Should().BeTrue();
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.Exists.Should().BeTrue();
        _ = observed.State.Color.Should().Be(color);
        _ = observed.State.AtmosphereEnabled.Should().BeTrue("setting background must not disable the authored atmosphere");
        var invalid = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeSetBackgroundColor(new Vector3(float.NaN))), this.TestContext.CancellationToken);
        _ = invalid.Status.Should().Be(RuntimeCommandStatus.Rejected);
        var unchanged = await commands.ObserveBackgroundAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = unchanged.State!.Color.Should().Be(color);
    }
}
