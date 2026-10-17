// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// What is specific to <see cref="RenameFolder"/>. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RenameFolderTests
{
    [TestMethod]
    public async Task NewName_RenamesAndRaisesLayoutChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RenameFolder(SampleScene.F, "Boxes")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutQuery.Folder(fixture.Scene, SampleScene.F)!.Name.Should().Be("Boxes");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task BlankName_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameFolder(SampleScene.F, " ")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MissingFolder_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameFolder(FolderId.New(), "Boxes")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SameName_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameFolder(SampleScene.F, "F")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }
}
