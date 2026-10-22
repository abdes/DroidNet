// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;

/// <summary>
/// What is specific to <see cref="RenameNode"/>. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RenameNodeTests
{
    [TestMethod]
    public async Task NewName_RenamesAndRaisesNodeRenamed()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Box");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.B));
    }

    [TestMethod]
    public async Task Name_IsTrimmed()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "  Box \t")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Metadata.Name.Should().Be("Box");
    }

    [TestMethod]
    [DataRow("", DisplayName = "Empty")]
    [DataRow(" ", DisplayName = "Space")]
    [DataRow("\t\n", DisplayName = "TabAndNewLine")]
    public async Task BlankName_IsInvalid(string name)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, name)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SameName_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "B")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }
}
