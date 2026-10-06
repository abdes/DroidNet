// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class PublicationGateTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DoNotParallelize]
    public async Task SelectionGateContentionDoesNotRaiseFirstChanceExceptions()
    {
        using var project = new ProjectDirectory();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        using var held = await CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token).ConfigureAwait(false);
        var exceptions = new ConcurrentQueue<Exception>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is CookOutputBusyException or IOException)
            {
                exceptions.Enqueue(args.Exception);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            var pending = CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token);
            await Task.Delay(120, cancellation.Token).ConfigureAwait(false);
            _ = pending.IsCompleted.Should().BeFalse();
            held.Dispose();
            using var acquired = await pending.WaitAsync(cancellation.Token).ConfigureAwait(false);
            _ = exceptions.Should().BeEmpty();
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }
    }
}
