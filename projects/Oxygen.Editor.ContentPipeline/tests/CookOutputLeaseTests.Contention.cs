// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Routine ownership contention must not flood the debugger with first-chance exceptions.</summary>
public sealed partial class CookOutputLeaseTests
{
    /// <summary>Both sides of the publication/inspection wait use non-throwing ownership checks.</summary>
    /// <param name="publisherWaits">Whether publication waits for an existing inspection.</param>
    /// <returns>The asynchronous contention regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    [DoNotParallelize]
    public async Task ExpectedContentionDoesNotRaiseFirstChanceExceptions(bool publisherWaits)
    {
        using var project = new ProjectDirectory();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        using var held = publisherWaits
            ? await CookOutputLease.AcquireInspectionAsync(project.Root, cancellation.Token).ConfigureAwait(false)
            : await CookOutputLease.AcquireWriteAsync(project.Root, cancellation.Token).ConfigureAwait(false);
        var exceptions = new ConcurrentQueue<Exception>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is CookOutputBusyException
                || (args.Exception is IOException && args.Exception.Message.Contains(project.Root, StringComparison.OrdinalIgnoreCase)))
            {
                exceptions.Enqueue(args.Exception);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            var waiting = AcquireWaitingAsync(project.Root, publisherWaits, cancellation.Token);
            await Task.Delay(220, cancellation.Token).ConfigureAwait(false);
            _ = waiting.IsCompleted.Should().BeFalse("the existing owner must retain exclusion while the waiter retries");
            held.Dispose();
            using var acquired = await waiting.WaitAsync(cancellation.Token).ConfigureAwait(false);
            _ = exceptions.Should().BeEmpty("normal lock contention must not throw even when the debugger observes first-chance exceptions");
        }
        finally
        {
            held.Dispose();
            await cancellation.CancelAsync().ConfigureAwait(false);
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }
    }

    /// <summary>A live preview rejection produces one actionable failure without an inner sharing-exception cascade.</summary>
    [TestMethod]
    [DoNotParallelize]
    public void PersistentPreviewConflictRaisesOnlyTheActionableFailure()
    {
        using var project = new ProjectDirectory();
        using var held = CookOutputLease.AcquireRead(project.Root);
        var exceptions = new List<Exception>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is CookOutputBusyException
                || (args.Exception is IOException && args.Exception.Message.Contains(project.Root, StringComparison.OrdinalIgnoreCase)))
            {
                exceptions.Add(args.Exception);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            Action publish = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
            _ = publish.Should().ThrowExactly<CookOutputBusyException>();
            _ = exceptions.Should().ContainSingle().Which.Should().BeOfType<CookOutputBusyException>();
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }
    }

    /// <summary>Reader release leaves deletion to a gate owner, and new registrations reclaim retired markers.</summary>
    [TestMethod]
    public void ReleasedReadersAreReclaimedOnlyUnderTheRegistrationGate()
    {
        using var project = new ProjectDirectory();
        var first = CookOutputLease.AcquireRead(project.Root);
        var directory = Path.Combine(project.Root, ".build", "cook", "readers");
        var marker = Directory.EnumerateFiles(directory, "*.lease").Single();
        first.Dispose();
        _ = File.Exists(marker).Should().BeTrue("release must not race a gate owner's check by deleting its marker");
        using var next = CookOutputLease.AcquireRead(project.Root);
        _ = File.Exists(marker).Should().BeFalse();
        _ = Directory.EnumerateFiles(directory, "*.lease").Should().ContainSingle();
        next.Dispose();
        using var writer = CookOutputLease.AcquireWrite(project.Root);
        _ = Directory.EnumerateFiles(directory, "*.lease").Should().BeEmpty();
    }

    private static async Task<IDisposable> AcquireWaitingAsync(string projectRoot, bool publisherWaits, CancellationToken cancellationToken)
        => publisherWaits
            ? await CookOutputLease.AcquireWriteAsync(projectRoot, cancellationToken).ConfigureAwait(false)
            : await CookOutputLease.AcquireInspectionAsync(projectRoot, cancellationToken).ConfigureAwait(false);
}
