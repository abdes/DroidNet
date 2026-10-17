// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// What is specific to <see cref="RestoreFolder"/> (scene-edits.md 4): it takes back, in order, the entries the
/// folder's removal promoted to its place. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RestoreFolderTests
{
    [TestMethod]
    public async Task RemovedFolder_IsRestoredWithItsChildren()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        _ = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.F, PromoteChildren: true)).ConfigureAwait(false);
        fixture.Changes.Clear();
        var restore = new RestoreFolder(FolderEntry(), LayoutPosition.AtRoot(1), [LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G)]);

        var result = await fixture.Authoring.ExecuteAsync(restore).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task ExistingId_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var restore = new RestoreFolder(FolderEntry(), LayoutPosition.AtRoot(1), []);

        var result = await fixture.Authoring.ExecuteAsync(restore).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderExists);
        SceneAssert.Unchanged(fixture, before);
    }

    // The entry of folder F as the sample declares it.
    private static ExplorerEntryData FolderEntry()
        => new() { Type = "Folder", FolderId = SampleScene.F, Name = "F", IsExpanded = true };
}
