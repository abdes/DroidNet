// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;

namespace DroidNet.Hosting.Tests.WinUI;

/// <summary>
/// Unit tests for the <see cref="DispatcherQueueExtensions"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[SuppressMessage("StyleCop.CSharp.ReadabilityRules", "SA1101:Prefix local calls with this", Justification = "Consistency with project style")]
public class DispatcherQueueExtensionsTests
{
    private DispatcherQueueController? controller;
    private DispatcherQueue? dispatcher;

    public TestContext TestContext { get; set; }

    /// <summary>
    /// Initializes the test environment by creating a dedicated UI thread and its associated dispatcher.
    /// </summary>
    [TestInitialize]
    public void Initialize()
    {
        this.controller = DispatcherQueueController.CreateOnDedicatedThread();
        this.dispatcher = this.controller.DispatcherQueue;
    }

    /// <summary>
    /// Cleans up the test environment by shutting down the dispatcher thread.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [TestCleanup]
    public async Task Cleanup()
    {
        if (this.controller != null)
        {
            await this.controller.ShutdownQueueAsync().AsTask(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
            this.controller = null;
        }
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_Action_OnUIThread_ExecutesSynchronously() =>
        await this.dispatcher!.DispatchAsync(() =>
        {
            // GIVEN
            var innerExecuted = false;
            _ = this.dispatcher!.HasThreadAccess.Should().BeTrue();

            // WHEN
            void InnerAction()
            {
                innerExecuted = true;
            }

            var innerTask = this.dispatcher!.DispatchAsync(InnerAction);

            // THEN
            _ = innerExecuted.Should().BeTrue();
            _ = innerTask.IsCompleted.Should().BeTrue();
            _ = innerTask.Should().Be(Task.CompletedTask);
        }).ConfigureAwait(false);

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_Action_OffUIThread_EnqueuesAndCompletes()
    {
        // GIVEN
        var executed = false;

        // WHEN
        _ = await this.dispatcher!.DispatchAsync(() => executed = true).ConfigureAwait(false);

        // THEN
        _ = executed.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_Action_OffUIThread_PropagatesException()
    {
        // GIVEN
        var expectedException = new InvalidOperationException("Test exception");

        // WHEN
        var act = () => this.dispatcher!.DispatchAsync(() => throw expectedException);

        // THEN
        _ = await act.Should().ThrowAsync<InvalidOperationException>().WithMessage(expectedException.Message).ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_FuncT_OffUIThread_ReturnsResult()
    {
        // GIVEN
        const int expectedResult = 42;

        // WHEN
        var result = await this.dispatcher!.DispatchAsync(() => expectedResult).ConfigureAwait(false);

        // THEN
        _ = result.Should().Be(expectedResult);
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_FuncTask_OffUIThread_AwaitsInnerTask()
    {
        // GIVEN
        var completed = false;

        // WHEN
        await this.dispatcher!.DispatchAsync(async () =>
        {
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(false);
            completed = true;
        }).ConfigureAwait(false);

        // THEN
        _ = completed.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_FuncTaskT_OffUIThread_ReturnsAwaitedResult()
    {
        // GIVEN
        const string expectedResult = "Success";

        // WHEN
        var result = await this.dispatcher!.DispatchAsync(async () =>
        {
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(false);
            return expectedResult;
        }).ConfigureAwait(false);

        // THEN
        _ = result.Should().Be(expectedResult);
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_FuncTask_OnUIThread_ReturnsInnerTaskDirectly()
    => await this.dispatcher!.DispatchAsync(async () =>
    {
        // GIVEN
        var innerTask = Task.Delay(10, this.TestContext.CancellationToken);

        // WHEN
        var resultTask = this.dispatcher!.DispatchAsync(() => innerTask);

        // THEN - should be the same task instance (proxying)
        _ = resultTask.Should().Be(innerTask);
        await resultTask.ConfigureAwait(false);
    }).ConfigureAwait(false);

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    public async Task DispatchAsync_ShutdownDispatcher_ThrowsInvalidOperationException()
    {
        // GIVEN
        await this.controller!.ShutdownQueueAsync().AsTask(this.TestContext.CancellationToken).ConfigureAwait(false);
        this.controller = null;

        // WHEN
        var act = () => this.dispatcher!.DispatchAsync(() => { });

        // THEN
        _ = await act.Should().ThrowAsync<InvalidOperationException>()
            .WithMessage("*shutting down*").ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory("Dispatcher")]
    [Timeout(5000, CooperativeCancellation = true)]
    [SuppressMessage("Meziantou.Analyzer", "MA0022:Return Task.FromResult instead of returning null", Justification = "Testing internal null validation")]
    public async Task DispatchAsync_TaskReturningNull_ThrowsInvalidOperationException()
    {
        // GIVEN
        static Task NullTaskFunc() => null!;

        // WHEN
        var act = () => this.dispatcher!.DispatchAsync(NullTaskFunc);

        // THEN
        _ = await act.Should().ThrowAsync<InvalidOperationException>()
            .WithMessage("*cannot be null*").ConfigureAwait(false);
    }
}
