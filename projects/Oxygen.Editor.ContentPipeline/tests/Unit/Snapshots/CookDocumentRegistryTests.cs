// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookDocumentRegistryTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Acquires all owners in stable order and releases each exactly once in reverse order.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ReadSetOrdersMultipleOwnersAndReleasesExactlyOnce()
    {
        var registry = new CookDocumentRegistry();
        var firstPath = Path.Combine(Path.GetTempPath(), "a.omat.json");
        var secondPath = Path.Combine(Path.GetTempPath(), "b.omat.json");
        List<string> calls = [];
        using var second = registry.Register(secondPath, _ => Acquire("second", secondPath));
        using var first = registry.Register(firstPath, _ => Acquire("first", firstPath));
        using var alias = registry.Register(firstPath, _ => Acquire("alias", firstPath));
        using var closed = registry.Register(firstPath, _ => Task.FromResult<CookDocumentReadLease?>(null));
        using var reads = await registry.AcquireAsync([secondPath, firstPath, firstPath], CancellationToken.None).ConfigureAwait(false);

        _ = reads.Documents.Should().HaveCount(3);
        _ = calls.Should().Equal("read first", "read alias", "read second");
        reads.Dispose();
        reads.Dispose();
        _ = calls.Should().Equal("read first", "read alias", "read second", "release second", "release alias", "release first");

        Task<CookDocumentReadLease?> Acquire(string name, string path)
        {
            calls.Add("read " + name);
            return Task.FromResult<CookDocumentReadLease?>(new(State(path), () => calls.Add("release " + name)));
        }
    }

    /// <summary>Releases earlier document gates when cancellation interrupts a later acquisition.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelledAcquisitionReleasesEarlierReads()
    {
        var registry = new CookDocumentRegistry();
        var firstPath = Path.Combine(Path.GetTempPath(), "a.omat.json");
        var secondPath = Path.Combine(Path.GetTempPath(), "b.omat.json");
        using var cancellation = new CancellationTokenSource();
        var released = false;
        using var first = registry.Register(
            firstPath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(State(firstPath), () => released = true)));
        using var second = registry.Register(secondPath, async token =>
        {
            await cancellation.CancelAsync().ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            return null;
        });

        var acquire = () => registry.AcquireAsync([firstPath, secondPath], cancellation.Token);
        _ = await acquire.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);

        _ = released.Should().BeTrue();
    }

    /// <summary>Retires a closed document registration without affecting another owner of the same source.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ClosedRegistrationDoesNotAcquireAgain()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "material.omat.json");
        var acquired = false;
        using var closed = registry.Register(path, _ =>
        {
            acquired = true;
            return Task.FromResult<CookDocumentReadLease?>(new(State(path), () => { }));
        });
        using var open = registry.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(new(State(path), () => { })));
        closed.Dispose();

        using var reads = await registry.AcquireAsync([path], CancellationToken.None).ConfigureAwait(false);

        _ = acquired.Should().BeFalse();
        _ = reads.Documents.Should().ContainSingle();
    }

    /// <summary>Updates are deduplicated, snapshots remain immutable, and owners of the same path stay independent.</summary>
    [TestMethod]
    public void StateSnapshotsRetainIndependentOwnersWithoutReadingSource()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "shared.omat.json");
        var changes = new List<CookDocumentStateChangedEventArgs>();
        registry.StateChanged += (_, change) => changes.Add(change);
        using var first = registry.Register(path, _ => throw new InvalidOperationException("State reads must not acquire the source."));
        using var second = registry.Register(path, _ => throw new InvalidOperationException("State reads must not acquire the source."));
        var saved = State(path);
        first.UpdateState(saved);
        first.UpdateState(saved);
        _ = changes.Should().ContainSingle();
        var before = registry.GetState();
        second.UpdateState(saved with { DocumentId = Guid.NewGuid(), IsDirty = true });
        _ = before.Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        _ = registry.GetState().Documents.Should().HaveCount(2);
        first.Dispose();
        first.UpdateState(saved with { IsDirty = true });
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        _ = registry.GetState().Version.Should().Be(3);
        _ = changes.Should().HaveCount(3);
    }

    /// <summary>A save is distinguishable from an edit; invalid owner updates leave state untouched.</summary>
    [TestMethod]
    public void StateUpdatesDistinguishSavedBytesAndRejectDifferentIdentity()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "material.omat.json");
        using var owner = registry.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        CookDocumentStateChangedEventArgs? latest = null;
        registry.StateChanged += (_, change) => latest = change;
        var initial = State(path);
        owner.UpdateState(initial);
        owner.UpdateState(initial with { Revision = 2, IsDirty = true });
        _ = latest!.SavedSourceChanged.Should().BeFalse();
        owner.UpdateState(initial with { Revision = 2, SavedRevision = 2, SavedContentHash = "new saved bytes" });
        _ = latest!.SavedSourceChanged.Should().BeTrue();
        var version = registry.GetState().Version;
        Action invalidSource = () => owner.UpdateState(initial with { SourcePath = path + ".other" });
        Action invalidOwner = () => owner.UpdateState(initial with { DocumentId = Guid.NewGuid() });
        _ = invalidSource.Should().Throw<ArgumentException>();
        _ = invalidOwner.Should().Throw<ArgumentException>();
        _ = registry.GetState().Version.Should().Be(version);
    }

    /// <summary>A failed presentation subscriber cannot break authoring state or starve other observers.</summary>
    [TestMethod]
    public void FailingStateObserverDoesNotInterruptOtherConsumers()
    {
        var registry = new CookDocumentRegistry();
        var observed = 0;
        registry.StateChanged += (_, _) => throw new InvalidOperationException("Broken presentation observer.");
        registry.StateChanged += (_, _) => observed++;
        var path = Path.Combine(Path.GetTempPath(), "material.omat.json");
        using var owner = registry.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        owner.UpdateState(State(path));
        owner.Dispose();
        _ = observed.Should().Be(2);
        _ = registry.GetState().Documents.Should().BeEmpty();
    }

    /// <summary>Relocation preserves document identity, updates source lookups and rejects a different owner.</summary>
    /// <returns>The asynchronous registration regression.</returns>
    [TestMethod]
    public async Task RelocateSourceChangesLookupAndStateWithoutReplacingDocumentOwner()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "Main.oscene.json");
        var renamedPath = Path.Combine(Path.GetTempPath(), "Demo.oscene.json");
        var state = State(path);
        using var registration = registry.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => { })));
        registration.UpdateState(state);
        var original = registry.GetState();
        var changes = new List<CookDocumentStateChangedEventArgs>();
        registry.StateChanged += (_, change) => changes.Add(change);

        state = state with { SourcePath = renamedPath, DisplayName = "Demo" };
        registration.RelocateSource(state);

        _ = original.Documents.Should().ContainSingle().Which.SourcePath.Should().Be(path);
        _ = registry.GetState().Documents.Should().ContainSingle().Which.Should().Be(state);
        _ = changes.Should().ContainSingle().Which.SavedSourceChanged.Should().BeTrue();
        using var oldReads = await registry.AcquireAsync([path], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = oldReads.Documents.Should().BeEmpty();
        using var newReads = await registry.AcquireAsync([renamedPath], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = newReads.Documents.Should().ContainSingle().Which.Should().Be(state);
        Action otherOwner = () => registration.RelocateSource(state with { SourcePath = path, DocumentId = Guid.NewGuid() });
        _ = otherOwner.Should().Throw<ArgumentException>();
        _ = registry.GetState().Documents.Should().ContainSingle().Which.Should().Be(state);
    }

    /// <summary>A capture waiting on the old source releases its gate and cancels after a rename.</summary>
    /// <returns>The asynchronous relocation race regression.</returns>
    [TestMethod]
    public async Task RelocateSourceCancelsReadThatWasWaitingForOldPath()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "Main.oscene.json");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var state = State(path);
        var released = false;
        using var registration = registry.Register(path, async token =>
        {
            entered.SetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
            return new(state, () => released = true);
        });
        registration.UpdateState(state);
        var read = registry.AcquireAsync([path], this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        state = state with { SourcePath = path + ".renamed" };
        registration.RelocateSource(state);
        release.SetResult();

        Func<Task> finishRead = async () => { using var reads = await read.ConfigureAwait(false); };
        _ = await finishRead.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = released.Should().BeTrue();
    }

    private static CookDocumentState State(string path) => new(Guid.NewGuid(), path, "Material", 1, 1, IsDirty: false, "saved hash");
}
