// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
public sealed class PendingPropertySyncQueueTests
{
    [TestMethod]
    public void FailedReplay_RemainsPendingUntilAcknowledged()
    {
        var (queue, scene, lifetime, node) = Create();
        var revision = new SceneSyncRevision(lifetime, 1, 1);
        _ = queue.Enqueue(scene, revision, node, [Entry(TransformField.PositionX, 3)]);
        var pending = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which;
        var failure = new SyncOutcome(SyncStatus.Failed, SceneOperationKinds.EditTransform, AffectedScope.Empty, Message: "Native rejection");

        queue.Fail(scene, pending, failure);

        _ = queue.Count(scene).Should().Be(1);
        _ = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which.Failure.Should().BeSameAs(failure);
        queue.Acknowledge(scene, pending);
        _ = queue.Count(scene).Should().Be(0);
    }

    [TestMethod]
    public void AcceptedSnapshot_DiscardsOlderValuesButPreservesLaterSameRevisionPreview()
    {
        var (queue, scene, lifetime, node) = Create();
        _ = queue.Enqueue(scene, new(lifetime, 1, 1), node, [Entry(TransformField.PositionX, 3)]);
        _ = queue.Enqueue(scene, new(lifetime, 2, 3), node, [Entry(TransformField.PositionY, 7)]);

        queue.Supersede(scene, new(lifetime, 2, 2));
        _ = queue.Enqueue(scene, new(lifetime, 1, 5), node, [Entry(TransformField.PositionZ, 99)]);

        var remaining = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which;
        _ = remaining.Entries.Should().ContainSingle().Which.Should().Be(Entry(TransformField.PositionY, 7));
    }

    [TestMethod]
    public void NewerField_ReplacesOnlyItsOlderValueAndLateAcknowledgmentCannotEraseIt()
    {
        var (queue, scene, lifetime, node) = Create();
        _ = queue.Enqueue(scene, new(lifetime, 1, 1), node, [Entry(TransformField.PositionX, 1), Entry(TransformField.PositionY, 2)]);
        var old = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which;

        _ = queue.Enqueue(scene, new(lifetime, 2, 2), node, [Entry(TransformField.PositionX, 9)]);

        _ = queue.Current(scene, old)!.Entries.Should().ContainSingle().Which.Should().Be(Entry(TransformField.PositionY, 2));
        queue.Acknowledge(scene, old);
        var current = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which;
        _ = current.Entries.Should().ContainSingle().Which.Should().Be(Entry(TransformField.PositionX, 9));
        queue.Acknowledge(scene, current);
        _ = queue.Enqueue(scene, new(lifetime, 1, 1), node, [Entry(TransformField.PositionX, 1)]);
        _ = queue.Count(scene).Should().Be(0, "late older delivery cannot replace an already accepted field value");
    }

    [TestMethod]
    public void ReopenedScene_RejectsOldLifetimePayloadsClosuresAndCompletions()
    {
        var (queue, scene, lifetime, node) = Create();
        _ = queue.Enqueue(scene, new(lifetime, 10, 1), node, [Entry(TransformField.PositionX, 1)]);
        var old = queue.Snapshot(scene, lifetime).Should().ContainSingle().Which;
        var reopened = Guid.NewGuid();
        queue.Register(scene, reopened);
        _ = queue.Enqueue(scene, new(reopened, 1, 1), node, [Entry(TransformField.PositionX, 9)]);

        queue.Close(scene, lifetime);
        queue.Supersede(scene, new(lifetime, 100, 100));
        queue.Acknowledge(scene, old);
        _ = queue.Enqueue(scene, new(lifetime, 101, 101), node, [Entry(TransformField.PositionX, 42)]);

        _ = queue.Snapshot(scene, lifetime).Should().BeEmpty();
        _ = queue.Snapshot(scene, reopened).Should().ContainSingle().Which.Entries.Should().ContainSingle()
            .Which.Value.Should().Be(9);
    }

    [TestMethod]
    public void Payloads_AreCopiedAndReplayedInAuthoredOrder()
    {
        var (queue, scene, lifetime, node) = Create();
        var later = new List<EnginePropertyValueEntry> { Entry(TransformField.PositionY, 7) };
        _ = queue.Enqueue(scene, new(lifetime, 2, 5), node, later);
        _ = queue.Enqueue(scene, new(lifetime, 1, 3), node, [Entry(TransformField.PositionX, 3)]);
        later[0] = Entry(TransformField.PositionY, 99);

        var pending = queue.Snapshot(scene, lifetime);

        _ = pending.Select(request => request.Revision.Revision).Should().Equal(1, 2);
        _ = pending[1].Entries.Should().ContainSingle().Which.Value.Should().Be(7);
        _ = queue.Count(scene).Should().Be(2, "reading a replay batch does not acknowledge it");
    }

    private static (PendingPropertySyncQueue queue, Guid scene, Guid lifetime, Guid node) Create()
    {
        var queue = new PendingPropertySyncQueue();
        var scene = Guid.NewGuid();
        var lifetime = Guid.NewGuid();
        queue.Register(scene, lifetime);
        return (queue, scene, lifetime, Guid.NewGuid());
    }

    private static EnginePropertyValueEntry Entry(TransformField field, float value)
        => new(EngineComponentId.Transform, (ushort)field, value);
}
