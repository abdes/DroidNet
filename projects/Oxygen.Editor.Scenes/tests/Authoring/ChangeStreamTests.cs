// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// The change stream (scene-edits.md 3.7): <see cref="SceneAuthoring.Changed"/> is raised after each edit and replay,
/// in order, once the scene already holds the new values.
/// </summary>
[TestClass]
public sealed class ChangeStreamTests
{
    [TestMethod]
    public async Task OneEdit_RaisesOneEvent()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task UndoAndRedo_RaiseOneEventEach()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        fixture.Changes.Clear();

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        var events = fixture.Changes.All;
        _ = events.Should().HaveCount(2);
        SceneAssert.Changes(events[0].Changes, new SceneChange.NodeRenamed(SampleScene.B));
        SceneAssert.Changes(events[1].Changes, new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task Handler_ReadsTheNewValueFromTheScene()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        string? nameSeenByHandler = null;
        fixture.Authoring.Changed += (_, args) =>
        {
            if (args.Changes.OfType<SceneChange.NodeRenamed>().SingleOrDefault() is { } renamed)
            {
                nameSeenByHandler = fixture.Node(renamed.Node).Name;
            }
        };

        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        _ = nameSeenByHandler.Should().Be("Box");
    }

    [TestMethod]
    public async Task ThreeEdits_RaiseInExecutionOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.A, "X")).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Y")).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.Cam, "Z")).ConfigureAwait(false);

        var events = fixture.Changes.All;
        _ = events.Should().HaveCount(3);
        SceneAssert.Changes(events[0].Changes, new SceneChange.NodeRenamed(SampleScene.A));
        SceneAssert.Changes(events[1].Changes, new SceneChange.NodeRenamed(SampleScene.B));
        SceneAssert.Changes(events[2].Changes, new SceneChange.NodeRenamed(SampleScene.Cam));
    }

    [TestMethod]
    public async Task RefusedEdit_RaisesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.B);
        _ = fixture.Changes.All.Should().BeEmpty();
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Event_CarriesTheDocumentId()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        _ = fixture.Changes.All.Should().ContainSingle().Which.Args.Document.Should().Be(fixture.Document);
    }
}
