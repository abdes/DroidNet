// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// <see cref="LayoutScopes"/> (scene-edits.md 4): what callers need to build layout edits, read from the sample's
/// explorer layout.
/// </summary>
[TestClass]
public sealed class LayoutScopesTests
{
    public static IEnumerable<TestDataRow<ScopeCase>> Scopes =>
    [
        new(new ScopeCase(LayoutPosition.AtRoot(0), Scope: null)) { DisplayName = "Root" },
        new(new ScopeCase(LayoutPosition.InFolder(SampleScene.F, 0), Scope: null)) { DisplayName = "FolderF" },
        new(new ScopeCase(LayoutPosition.InFolder(SampleScene.G, 0), Scope: null)) { DisplayName = "NestedG" },
        new(new ScopeCase(LayoutPosition.UnderNode(SampleScene.A, 0), SampleScene.A)) { DisplayName = "NodeA" },
        new(new ScopeCase(LayoutPosition.InFolder(SampleScene.AF, 0), SampleScene.A)) { DisplayName = "FolderAF" },
    ];

    public static IEnumerable<TestDataRow<EntriesCase>> Containers =>
    [
        new(new EntriesCase(
            LayoutPosition.AtRoot(0),
            [
                LayoutEntryId.Of(SampleScene.A),
                LayoutEntryId.Of(SampleScene.F),
                LayoutEntryId.Of(SampleScene.Cam),
                LayoutEntryId.Of(SampleScene.Ortho),
                LayoutEntryId.Of(SampleScene.Sun),
                LayoutEntryId.Of(SampleScene.Moon),
                LayoutEntryId.Of(SampleScene.Lamp),
                LayoutEntryId.Of(SampleScene.Spot),
                LayoutEntryId.Of(SampleScene.Fog),
                LayoutEntryId.Of(SampleScene.Mesh),
                LayoutEntryId.Of(SampleScene.Script),
                LayoutEntryId.Of(SampleScene.Car),
            ])) { DisplayName = "Root" },
        new(new EntriesCase(LayoutPosition.UnderNode(SampleScene.A, 0), [LayoutEntryId.Of(SampleScene.A1), LayoutEntryId.Of(SampleScene.AF)])) { DisplayName = "NodeA" },
        new(new EntriesCase(LayoutPosition.InFolder(SampleScene.F, 0), [LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G)])) { DisplayName = "FolderF" },
        new(new EntriesCase(LayoutPosition.InFolder(SampleScene.G, 0), [])) { DisplayName = "FolderG" },
    ];

    // F holds B and G.
    public static IEnumerable<TestDataRow<EndCase>> Ends =>
    [
        new(new EndCase(Moving: null, Index: 2)) { DisplayName = "NothingMoving" },
        new(new EndCase([LayoutEntryId.Of(SampleScene.B)], Index: 1)) { DisplayName = "MovingB" },
        new(new EndCase([LayoutEntryId.Of(SampleScene.Cam)], Index: 2)) { DisplayName = "MovingAnEntryOfAnotherContainer" },
    ];

    [TestMethod]
    [DynamicData(nameof(Scopes))]
    public async Task ScopeOf(ScopeCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var found = LayoutScopes.TryScopeOf(fixture.Layout, row.Position, out var scope);

        _ = found.Should().BeTrue();
        _ = scope.Should().Be(row.Scope);
    }

    [TestMethod]
    public async Task PositionOf_ReturnsTheEntryPosition()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var position = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.B));

        _ = position.Should().Be(LayoutPosition.InFolder(SampleScene.F, 0));
    }

    [TestMethod]
    [DynamicData(nameof(Containers))]
    public async Task EntriesOf(EntriesCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var entries = LayoutScopes.EntriesOf(fixture.Layout, row.Container);

        _ = entries.Should().Equal(row.Expected);
    }

    [TestMethod]
    public async Task EntriesOf_IgnoresTheIndex()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var entries = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 7));

        _ = entries.Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));
    }

    [TestMethod]
    public async Task EntriesOf_MissingContainer_IsEmpty()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var entries = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(FolderId.New(), 0));

        _ = entries.Should().BeEmpty();
    }

    [TestMethod]
    [DynamicData(nameof(Ends))]
    public async Task EndOf(EndCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var end = LayoutScopes.EndOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0), row.Moving);

        _ = end.Should().Be(LayoutPosition.InFolder(SampleScene.F, row.Index));
    }

    [TestMethod]
    public async Task EndOf_MoveToTheEnd_Applies()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        LayoutEntryId[] moving = [LayoutEntryId.Of(SampleScene.B)];
        var end = LayoutScopes.EndOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0), moving);

        var result = await fixture.Authoring.ExecuteAsync(new MoveLayoutEntries(moving, end)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.G), LayoutEntryId.Of(SampleScene.B));
    }
}
