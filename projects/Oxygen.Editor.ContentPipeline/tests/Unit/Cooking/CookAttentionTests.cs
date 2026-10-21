// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookCoordinatorScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookAttentionTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>When a user's queue drains, a failed run takes precedence over the last successful run.</summary>
    /// <returns>The asynchronous queue outcome test.</returns>
    [TestMethod]
    public async Task CompletedQueueRevealsProblemsBeforeSuccessfulResults()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var events = new ConcurrentQueue<CookRunChangedEventArgs>();
        coordinator.RunChanged += (_, change) => events.Enqueue(change);
        var finish = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => finish.Task, CancellationToken.None);
        var firstId = coordinator.Runs.Single().OperationId;
        var second = coordinator.RunCookAsync(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Other.omat.json")), (_, _) => Task.FromResult(2), CancellationToken.None);
        events.Clear();
        finish.SetException(new IOException("First cook failed."));
        Func<Task> failed = () => first;
        _ = await failed.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = await second.ConfigureAwait(false);
        _ = events.Where(static change => change.Reveal).Should().ContainSingle().Which.Run.OperationId.Should().Be(firstId);
        _ = coordinator.Runs.Should().OnlyContain(static run => run.IsCompleted);
    }
}
