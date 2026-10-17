// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks that a scene editor builds its panes however it learns that its scene is in the engine.</summary>
public sealed partial class SceneEditorConflictTests
{
    /// <summary>An editor created after its scene finished synchronizing builds its panes at once.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task EditorCreatedAfterSynchronizationBuildsItsPanes()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true, synchronized: true).ConfigureAwait(false);

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>
    ///     The sync service's own notification builds the panes when the relayed scene-loaded message never
    ///     arrives, as when another tab is active at that moment during a scene switch.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SynchronizationWithoutTheRelayedMessageBuildsThePanes()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: false).ConfigureAwait(false);
        _ = fixture.Editor.Viewports.Should().BeEmpty();

        RaiseSceneSynchronized(fixture, fixture.Tab.DocumentId);

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>A notification for another document instance leaves the editor waiting.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SynchronizationOfAnotherDocumentIsIgnored()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);

        RaiseSceneSynchronized(fixture, DocumentId.New());

        _ = fixture.Editor.Viewports.Should().BeEmpty();
    }

    // The sync service raises its notification synchronously, as the engine's completion does.
    private static void RaiseSceneSynchronized(Fixture fixture, DocumentId document)
        => fixture.Sync.Raise(value => value.SceneSynchronized += null, fixture.Sync.Object, new SceneSynchronizationCompletedEventArgs(fixture.Scene, document));
}
