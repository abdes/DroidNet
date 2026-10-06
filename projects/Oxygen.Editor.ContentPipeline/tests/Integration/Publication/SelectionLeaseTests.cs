// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class SelectionLeaseTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task CancellingSelectionWaitDoesNotReleaseAnotherPublisher()
    {
        using var project = new ProjectDirectory();
        using var held = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        var waiting = CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        _ = await ((Func<Task>)(() => waiting)).Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var next = CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken);
        _ = next.IsCompleted.Should().BeFalse();
        held.Dispose();
        using var acquired = await next.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = acquired.ProjectRoot.Should().Be(project.Root);
    }
}
