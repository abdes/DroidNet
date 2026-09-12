// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks new cooked material values on an existing native scene binding.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Refreshing the same material key changes its bound colour without reloading the scene.</summary>
    /// <returns>The asynchronous native refresh regression.</returns>
    [TestMethod]
    public Task RecookedMaterialUpdatesExistingNativeBindingWithoutSceneReload() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(40));
        var red = new Vector4(1, 0, 0, 1);
        var green = new Vector4(0, 1, 0, 1);
        var (uri, key) = await fixture.CookTestMaterialAsync("Refresh", timeout.Token, red).ConfigureAwait(true);
        var root = Path.Combine(fixture.ProjectRoot, ".cooked", "Content");
        await fixture.InitializeAsync(timeout.Token, root).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [node.Id], 0, uri, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, timeout.Token).ConfigureAwait(true);
        var source = fixture.Source;
        var revision = fixture.Context.Metadata.ChangeVersion;
        var historyCount = fixture.Context.History.UndoStack.Count;
        await fixture.SuspendCookedContentAsync().WaitAsync(timeout.Token).ConfigureAwait(true);
        var (_, recookedKey) = await fixture.CookTestMaterialAsync("Refresh", timeout.Token, green).ConfigureAwait(true);
        _ = recookedKey.Should().Be(key);
        await fixture.RefreshCookedRootsAsync(root).WaitAsync(timeout.Token).ConfigureAwait(true);
        var refreshed = await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true);
        _ = refreshed.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(green);
        _ = refreshed.MaterialKeys.Should().ContainSingle().Which.Should().BeEquivalentTo(key);
        _ = fixture.Source.Should().BeSameAs(source);
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(historyCount);
    });
}
