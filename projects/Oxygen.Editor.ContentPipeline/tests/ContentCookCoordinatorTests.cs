// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies writer serialization, project cancellation, and retained worker ownership.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Paused saves stay visible without holding the writer needed by explicit cooks.</summary>
    /// <returns>The asynchronous pause regression.</returns>
    [TestMethod]
    public async Task PausedAutomaticWorkRemainsQueuedWhileExplicitCookRuns()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        coordinator.IsAutomaticCookingPaused = true;
        var entered = false;
        var background = coordinator.RunCookAsync(
            new(CookTargetKind.Asset, new Uri("asset:///Content/Scene.oscene.json"), IsAutomatic: true),
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(1);
            },
            this.TestContext.CancellationToken);
        _ = entered.Should().BeFalse();
        _ = coordinator.Runs.Single().State.Should().Be(Cooking.CookRunState.Queued);
        _ = (await coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => Task.FromResult(2), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(2);
        coordinator.IsAutomaticCookingPaused = false;
        _ = (await background.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(1);
    }

    /// <summary>Prevents queued work from reading inputs before it owns the writer.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task QueuedWorkCapturesAfterPreviousWriterCompletes()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunAsync(
            async (_, _) =>
            {
                await release.Task.ConfigureAwait(false);
                return 1;
            },
            CancellationToken.None);
        var savedValue = "old";
        var entered = false;
        var second = coordinator.RunAsync(
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(savedValue);
            },
            CancellationToken.None);

        _ = entered.Should().BeFalse();
        savedValue = "new";
        release.SetResult();
        _ = await first.ConfigureAwait(false);
        _ = (await second.ConfigureAwait(false)).Should().Be("new");
    }

    /// <summary>Cancels a queued request without entering its input-capture delegate.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelledQueuedWorkNeverStarts()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        using var cancellation = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunAsync((_, _) => release.Task, CancellationToken.None);
        var entered = false;
        var second = coordinator.RunAsync(
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(2);
            },
            cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        var cancelled = async () => await second.ConfigureAwait(false);
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = entered.Should().BeFalse();
        release.SetResult(1);
        _ = await first.ConfigureAwait(false);
    }

    /// <summary>Rejects old project completions and prevents old queued work from crossing activation.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ProjectSwitchCancelsActiveAndQueuedWork()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var cancellationSeen = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunAsync(
            async (_, token) =>
            {
                var registration = token.Register(() => cancellationSeen.TrySetResult());
                await using var lifetime = registration.ConfigureAwait(false);
                return await release.Task.ConfigureAwait(false);
            },
            CancellationToken.None);
        var oldQueuedEntered = false;
        var oldQueued = coordinator.RunAsync(
            (_, _) =>
            {
                oldQueuedEntered = true;
                return Task.FromResult(2);
            },
            CancellationToken.None);
        var nextProject = CreateContext();
        context.Activate(nextProject);
        await cancellationSeen.Task.WaitAsync(TimeSpan.FromSeconds(5), CancellationToken.None).ConfigureAwait(false);
        var next = coordinator.RunAsync((operation, _) => Task.FromResult(operation.Project.ProjectId), CancellationToken.None);
        _ = next.IsCompleted.Should().BeFalse();
        release.SetResult(1);

        var firstCompletion = async () => await first.ConfigureAwait(false);
        var oldCompletion = async () => await oldQueued.ConfigureAwait(false);
        _ = await firstCompletion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = await oldCompletion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = oldQueuedEntered.Should().BeFalse();
        _ = (await next.ConfigureAwait(false)).Should().Be(nextProject.ProjectId);
    }

    /// <summary>Retains the writer after a termination failure until the worker drain really finishes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task TerminationFailureRetainsWriterUntilDrain()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failure = new ContentPipelineTerminationException(new Win32Exception(5), drain.Task);
        var first = coordinator.RunAsync<int>((_, _) => Task.FromException<int>(failure), CancellationToken.None);
        var failed = async () => await first.ConfigureAwait(false);
        _ = await failed.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);

        var entered = false;
        var second = coordinator.RunAsync(
            (_, _) =>
            {
                entered = true;
                return Task.FromResult(2);
            },
            CancellationToken.None);
        _ = entered.Should().BeFalse();
        drain.SetException(new IOException("reader failure after the tree stopped"));
        _ = (await second.WaitAsync(TimeSpan.FromSeconds(5), CancellationToken.None).ConfigureAwait(false)).Should().Be(2);
    }

    /// <summary>Releases the writer after a failure that has no outstanding process ownership.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task OrdinaryFailureReleasesWriter()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var first = coordinator.RunAsync<int>((_, _) => Task.FromException<int>(new IOException("input missing")), CancellationToken.None);
        var failed = async () => await first.ConfigureAwait(false);
        _ = await failed.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = (await coordinator.RunAsync((_, _) => Task.FromResult(2), CancellationToken.None).ConfigureAwait(false)).Should().Be(2);
    }

    /// <summary>Does not report success if a delegate finishes after caller cancellation.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelledActiveWorkCannotReturnSuccess()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        using var cancellation = new CancellationTokenSource();
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, cancellation.Token);

        await cancellation.CancelAsync().ConfigureAwait(false);
        release.SetResult(1);
        var completion = async () => await active.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await coordinator.RunAsync((_, _) => Task.FromResult(2), CancellationToken.None).ConfigureAwait(false)).Should().Be(2);
    }

    /// <summary>Treats reopening the same project context as a different lifetime.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ReopeningSameProjectInvalidatesOldOperation()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var operation = await coordinator.RunAsync((current, _) => Task.FromResult(current), CancellationToken.None).ConfigureAwait(false);
        context.Close();
        context.Activate(operation.Project);
        Action staleCallback = () => coordinator.VerifyCurrent(operation);
        _ = staleCallback.Should().Throw<OperationCanceledException>();
    }

    /// <summary>Allows an outstanding request to drain after coordinator disposal.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DisposalCancelsAndDrainsActiveOwnership()
    {
        var context = CreateContextService();
        using var coordinator = CreateCoordinator(context);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var active = coordinator.RunAsync((_, _) => release.Task, CancellationToken.None);
        coordinator.Dispose();
        release.SetResult(1);
        var completion = async () => await active.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var next = async () => await coordinator.RunAsync((_, _) => Task.FromResult(2), CancellationToken.None).ConfigureAwait(false);
        _ = await next.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(false);
    }

    private static ContentCookCoordinator CreateCoordinator(ProjectContextService context)
        => new(context, NullLogger<ContentCookCoordinator>.Instance);

    private static ProjectContextService CreateContextService()
    {
        var service = new ProjectContextService();
        service.Activate(CreateContext());
        return service;
    }

    private static ProjectContext CreateContext()
        => new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Cook coordination",
            Category = Category.Games,
            ProjectRoot = Path.GetTempPath(),
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };
}
