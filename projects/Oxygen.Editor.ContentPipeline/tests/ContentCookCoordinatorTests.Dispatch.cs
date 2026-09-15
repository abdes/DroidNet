// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Ensures synchronous cook work never monopolizes the submitting thread.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Submission returns while an admitted cook is still performing synchronous work.</summary>
    /// <param name="coalesced">Whether the request uses shared pending ownership.</param>
    /// <returns>The asynchronous dispatch regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SynchronousCookWorkDoesNotBlockItsSubmittingThread(bool coalesced)
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        using var release = new ManualResetEventSlim();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var returned = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var work = Task.Run(
            async () =>
        {
            var operation = coordinator.RunCookAsync(
                new(CookTargetKind.Project, ScopeUri: null) { CoalescePending = coalesced },
                (_, token) =>
            {
                entered.SetResult();
                release.Wait(token);
                return Task.FromResult(7);
            },
                this.TestContext.CancellationToken);
            returned.SetResult();
            return await operation.ConfigureAwait(false);
        },
            this.TestContext.CancellationToken);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            await returned.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = work.IsCompleted.Should().BeFalse();
        }
        finally
        {
            release.Set();
            _ = (await work.ConfigureAwait(false)).Should().Be(7);
        }
    }
}
