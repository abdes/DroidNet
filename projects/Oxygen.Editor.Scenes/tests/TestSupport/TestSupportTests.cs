// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The few guarantees that every other test relies on: the sample is the documented scene, and the shared helpers
/// report what really happened.
/// </summary>
[TestClass]
public sealed class TestSupportTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task SampleScene_DecodesAsDocumented()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().Equal(SampleScene.AllNodes);
        _ = Outline(fixture.Scene, fixture.Layout).Should().Be(
            "A(A1(A11) AF+(A2)) F+(B G-()) Cam Ortho Sun Moon Lamp Spot Fog Mesh Script Car");
        _ = fixture.Scene.AllNodes.Single(static node => node.Id == SampleScene.A11).Hierarchy.Parent!.Id.Should().Be(SampleScene.A1);
        _ = fixture.Scene.AllNodes.Single(static node => node.Id == SampleScene.A2).Hierarchy.Parent!.Id.Should().Be(SampleScene.A);
    }

    [TestMethod]
    public void SampleScene_BuildsTheSameDocumentEachTime()
        => JsonSerializer.Serialize(SampleScene.Build(), SceneFileJsonContext.Default.SceneFileData)
            .Should().Be(JsonSerializer.Serialize(SampleScene.Build(), SceneFileJsonContext.Default.SceneFileData));

    [TestMethod]
    public async Task BuilderWithout_DropsSubtreeAndItsLayout()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Builder().Without(SampleScene.A1).Build()).ConfigureAwait(false);

        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain([SampleScene.A1, SampleScene.A11]);
        _ = Outline(fixture.Scene, fixture.Layout).Should().StartWith("A(AF+(A2)) F+(B G-())");
    }

    [TestMethod]
    public async Task RoundTrip_RenamingProbe_RestoresTheDocumentEachStep()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        _ = await SceneAssert.RoundTripAsync(fixture, ProbeEdit.Renaming(SampleScene.B, "B", "Box")).ConfigureAwait(false);

        _ = fixture.Snapshot().Document.Should().Be(before.Document);
        _ = fixture.Changes.Count.Should().Be(4);
    }

    [TestMethod]
    public async Task ThreadFixture_RunsEditsOnItsThread()
    {
        using var fixture = await AuthoringFixture.CreateOnThreadAsync(SampleScene.Build()).ConfigureAwait(false);
        var probe = new ProbeEdit();

        var result = await fixture.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = probe.Log.Calls.Select(static call => call.ThreadId).Should().AllBeEquivalentTo(fixture.Thread!.ThreadId);
        _ = fixture.Changes.All.Should().ContainSingle().Which.ThreadId.Should().Be(fixture.Thread.ThreadId);
    }

    // "A(A1 ...)" is a node with layout children, "F+(...)" an expanded folder, "G-(...)" a collapsed one.
    private static string Outline(Scene scene, IEnumerable<ExplorerEntryData> entries)
        => string.Join(' ', entries.Select(entry =>
        {
            var name = entry.NodeId is { } node
                ? scene.AllNodes.Single(n => n.Id == node).Metadata.Name
                : entry.Name + (entry.IsExpanded == true ? "+" : "-");
            return entry.Children is { } children && (children.Count > 0 || entry.FolderId is not null)
                ? $"{name}({Outline(scene, children)})"
                : name;
        }));
}
