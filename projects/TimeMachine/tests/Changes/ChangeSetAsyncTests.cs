// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.TimeMachine.Changes;
using Moq;

namespace DroidNet.TimeMachine.Tests.Changes;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes.Async")]
public class ChangeSetAsyncTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ApplyAsync_ShouldInvokeApplyAsyncOnAllChanges()
    {
        // Arrange
        var changeMock1 = new Mock<IChange>();
        var changeMock2 = new Mock<IChange>();
        _ = changeMock1.Setup(c => c.ApplyAsync(It.IsAny<CancellationToken>())).Returns(ValueTask.CompletedTask);
        _ = changeMock2.Setup(c => c.ApplyAsync(It.IsAny<CancellationToken>())).Returns(ValueTask.CompletedTask);

        var changeSet = new ChangeSet { Key = new object() };
        changeSet.Add(changeMock1.Object);
        changeSet.Add(changeMock2.Object);

        // Act
        await changeSet.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        changeMock1.Verify(c => c.ApplyAsync(It.IsAny<CancellationToken>()), Times.Once);
        changeMock2.Verify(c => c.ApplyAsync(It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task ApplyAsync_ShouldAwaitChangesSequentially()
    {
        // Arrange
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var allowFirstToFinish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var secondStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var first = new BlockingAsyncChange(
            onStarted: firstStarted.SetResult,
            onProceed: allowFirstToFinish.Task);

        var second = new BlockingAsyncChange(
            onStarted: secondStarted.SetResult,
            onProceed: Task.CompletedTask);

        var changeSet = new ChangeSet { Key = new object() };

        // Add in reverse because ChangeSet.Add inserts at the beginning.
        changeSet.Add(second);
        changeSet.Add(first);

        // Act
        var applyTask = changeSet.ApplyAsync(this.TestContext.CancellationToken).AsTask();

        // Assert
        await firstStarted.Task.ConfigureAwait(false);
        _ = secondStarted.Task.IsCompleted.Should().BeFalse();

        allowFirstToFinish.SetResult();
        await applyTask.ConfigureAwait(false);

        _ = secondStarted.Task.IsCompleted.Should().BeTrue();
    }

    private sealed class BlockingAsyncChange(Action onStarted, Task onProceed) : IChange
    {
        private readonly Action onStarted = onStarted;
        private readonly Task onProceed = onProceed;

        public object Key { get; } = Guid.NewGuid();

        public void Apply() => throw new InvalidOperationException("This test change is async-only");

        public async ValueTask ApplyAsync(CancellationToken cancellationToken = default)
        {
            this.onStarted();
            await this.onProceed.ConfigureAwait(false);
        }
    }
}
