// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// What is specific to <see cref="CreateFolder"/> (scene-edits.md 4). Targets, locks, undo and the change stream of
/// every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class CreateFolderTests
{
    // The sample's root holds eleven entries; F holds two, A's entry holds two, G is empty.
    public static IEnumerable<TestDataRow<LayoutPosition>> Places =>
    [
        new(LayoutPosition.AtRoot(11)) { DisplayName = "RootEnd" },
        new(LayoutPosition.AtRoot(0)) { DisplayName = "RootStart" },
        new(LayoutPosition.InFolder(SampleScene.F, 1)) { DisplayName = "InsideF" },
        new(LayoutPosition.InFolder(SampleScene.G, 0)) { DisplayName = "InsideNestedG" },
        new(LayoutPosition.UnderNode(SampleScene.A, 1)) { DisplayName = "UnderNodeA" },
    ];

    [TestMethod]
    [DynamicData(nameof(Places))]
    public async Task At_CreatesTheFolderThere(LayoutPosition at)
    {
        ArgumentNullException.ThrowIfNull(at);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var folder = FolderId.New();
        var entriesBefore = LayoutQuery.EntryCount(fixture.Scene);

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(folder, at, "New")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(folder)).Should().Be(at);
        _ = LayoutQuery.EntryCount(fixture.Scene).Should().Be(entriesBefore + 1);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task Name_IsTrimmed()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var folder = FolderId.New();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(folder, LayoutPosition.AtRoot(0), " New ")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutQuery.Folder(fixture.Scene, folder)!.Name.Should().Be("New");
    }

    [TestMethod]
    [DataRow("", DisplayName = "Empty")]
    [DataRow(" ", DisplayName = "Space")]
    public async Task BlankName_IsInvalid(string name)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutPosition.AtRoot(0), name)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ExistingFolderId_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(SampleScene.F, LayoutPosition.AtRoot(0), "Again")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderExists);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task InMissingFolder_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutPosition.InFolder(FolderId.New(), 0), "New")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnderMissingNode_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var missing = NodeId.New();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutPosition.UnderNode(missing, 0), "New")).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetMissing, missing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnderLockedNode_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(FolderId.New(), LayoutPosition.UnderNode(SampleScene.A, 0), "New")).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.A);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task OnSceneWithoutLayout_SeedsItAndUndoRemovesIt()
    {
        // The document decodes a scene without a layout into one with an entry for every node, so this is the only
        // way a scene "without layout" reaches an edit; the folder joins that seeded layout.
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build() with { ExplorerLayout = null }).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var folder = FolderId.New();
        var roots = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0));

        var result = await fixture.Authoring.ExecuteAsync(new CreateFolder(folder, LayoutPosition.AtRoot(0), "New")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0)).Should().Equal(roots.Prepend(LayoutEntryId.Of(folder)));

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
        _ = LayoutScopes.PositionOf(fixture.Scene, LayoutEntryId.Of(folder)).Should().BeNull();
    }
}
