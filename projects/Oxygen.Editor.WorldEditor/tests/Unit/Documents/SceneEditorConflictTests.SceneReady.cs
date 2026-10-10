// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks that a scene editor builds its panes however it learns that its scene is in the engine.</summary>
public sealed partial class SceneEditorConflictTests
{
    /// <summary>An editor created after its scene finished synchronizing builds its panes at once.</summary>
    [TestMethod]
    public void EditorCreatedAfterSynchronizationBuildsItsPanes()
    {
        using var fixture = new Fixture(active: true, synchronized: true);

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>
    ///     The sync service's own notification builds the panes when the relayed scene-loaded message never
    ///     arrives, as when another tab is active at that moment during a scene switch.
    /// </summary>
    [TestMethod]
    public void SynchronizationWithoutTheRelayedMessageBuildsThePanes()
    {
        using var fixture = new Fixture(active: false);
        _ = fixture.Editor.Viewports.Should().BeEmpty();

        fixture.Sync.Raise(
            value => value.SceneSynchronized += null,
            fixture.Sync.Object,
            new SceneSynchronizationCompletedEventArgs(fixture.Scene, fixture.Metadata, new RuntimeSceneTarget(Guid.NewGuid(), fixture.Scene.Id, Guid.NewGuid(), Guid.NewGuid())));

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>A notification for another document instance leaves the editor waiting.</summary>
    [TestMethod]
    public void SynchronizationOfAnotherDocumentIsIgnored()
    {
        using var fixture = new Fixture(active: true);

        fixture.Sync.Raise(
            value => value.SceneSynchronized += null,
            fixture.Sync.Object,
            new SceneSynchronizationCompletedEventArgs(fixture.Scene, new(fixture.Scene.Id), new RuntimeSceneTarget(Guid.NewGuid(), fixture.Scene.Id, Guid.NewGuid(), Guid.NewGuid())));

        _ = fixture.Editor.Viewports.Should().BeEmpty();
    }
}
