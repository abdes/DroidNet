// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Live presentation state does not acquire source gates or revive closed owners.</summary>
public sealed partial class CookDocumentRegistryTests
{
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
}
