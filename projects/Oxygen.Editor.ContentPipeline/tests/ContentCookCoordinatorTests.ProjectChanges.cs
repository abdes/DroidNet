// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks that a committed project change retains writer ownership through native refresh.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Context replacement does not cancel the committed change or allow a competing cook to enter early.</summary>
    /// <returns>The asynchronous project-change ownership regression.</returns>
    [TestMethod]
    public async Task ProjectChangeOwnsTheWriterThroughContextReplacementAndResume()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var replaced = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var resume = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var change = coordinator.RunProjectChangeAsync(
            async (operation, token) =>
            {
                coordinator.VerifyWriter(operation);
                token.ThrowIfCancellationRequested();
                context.Activate(operation.Project with { Thumbnail = "updated.png" });
                replaced.SetResult();
                await resume.Task.ConfigureAwait(false);
            },
            this.TestContext.CancellationToken);
        await replaced.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = coordinator.IsAutomaticCookingPaused.Should().BeTrue();
        var entered = false;
        var cook = coordinator.RunAsync(
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(1);
            },
            this.TestContext.CancellationToken);
        _ = entered.Should().BeFalse();
        resume.SetResult();
        await change.ConfigureAwait(false);
        _ = (await cook.ConfigureAwait(false)).Should().Be(1);
    }
}
