// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookSavedSourceReaderTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source read queued before rename cancels without opening the vanished source.</summary>
    /// <returns>The asynchronous rename race regression.</returns>
    [TestMethod]
    public async Task RenamedSourceCancelsQueuedReadBeforeOpeningOldFile()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"), "Main.oscene.json");
        var state = new CookDocumentState(Guid.NewGuid(), path, "Main", 1, 1, IsDirty: false, "hash");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var released = false;
        using var registration = registry.Register(path, async token =>
        {
            entered.SetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
            return new(state, () => released = true);
        });
        registration.UpdateState(state);
        var read = CookSavedSourceReader.ReadAsync(registry, path, this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        state = state with { SourcePath = Path.Combine(Path.GetDirectoryName(path)!, "Demo.oscene.json") };
        registration.RelocateSource(state);
        release.SetResult();

        Func<Task> finishRead = async () => _ = await read.ConfigureAwait(false);
        _ = await finishRead.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = released.Should().BeTrue();
    }

    /// <summary>Missing settings and a first cook's missing provenance are ordinary absence, without a thrown exception.</summary>
    /// <param name="missingParent">Whether the parent directory is also absent.</param>
    /// <returns>The asynchronous read regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MissingOptionalInputsDoNotThrowFirstChanceExceptions(bool missingParent)
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        var path = missingParent ? Path.Combine(root, "Scene.oscene.json.import.json") : root + ".import.json";
        var exceptions = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is IOException && args.Exception.Message.Contains(root, StringComparison.OrdinalIgnoreCase))
            {
                exceptions.Enqueue(args.Exception);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            _ = CookSavedSourceReader.Exists(path).Should().BeFalse();
            var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
            _ = (await files.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Version.Exists.Should().BeFalse();
            _ = exceptions.Should().BeEmpty();
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }
    }
}
