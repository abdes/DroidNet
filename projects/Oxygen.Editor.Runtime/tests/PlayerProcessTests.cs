// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks how a player process that ends before it accepts the editor is reported.</summary>
[TestClass]
public sealed class PlayerProcessTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task StartAsync_APlayerThatEndsFirstReportsItsLastOutput()
    {
        // where.exe takes the launch arguments as a file pattern, prints why it found nothing and exits with code 1,
        // like a player that refuses its launch file.
        var whereExe = Path.Combine(Environment.SystemDirectory, "where.exe");

        var start = async () => await PlayerProcess.StartAsync(whereExe, "no-such-launch-file.json", logger: null, this.TestContext.CancellationToken).ConfigureAwait(false);

        var failure = (await start.Should().ThrowAsync<PlayerLaunchException>().ConfigureAwait(false)).Which;
        _ = failure.Message.Should().Contain("exited with code 1");
        _ = failure.PlayerOutput.Should().NotBeEmpty("the player's output says why it ended");
    }
}
