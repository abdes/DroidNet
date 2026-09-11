// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks cooked material selection, clear and mixed state against native bindings.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Material picker edits preserve both mixed values through history and saved reload.</summary>
    /// <param name="choice">The material picker row to apply.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("New")]
    [DataRow("None")]
    [DataRow("Default")]
    public Task MaterialPickerMixedSelectionHistoryAndReopenReachNativeState(string choice) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            AddGeometryNode(scene, "Cube");
            AddGeometryNode(scene, "Cube");
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(40));
        var (originalUri, originalKey) = await fixture.CookTestMaterialAsync("Original", timeout.Token).ConfigureAwait(true);
        var (_, nextKey) = await fixture.CookTestMaterialAsync("New", timeout.Token).ConfigureAwait(true);
        await fixture.InitializeAsync(timeout.Token, Path.Combine(fixture.ProjectRoot, ".cooked", "Content")).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var nodes = fixture.Source.RootNodes.Select(node => node.Id).ToArray();
        var defaultState = await AssertGeometryAsync(fixture, nodes[1], "Cube", timeout.Token).ConfigureAwait(true);
        var defaultKey = defaultState.MaterialKeys.Should().ContainSingle().Which;
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [nodes[0]], 0, originalUri, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await AssertMaterialAsync(fixture, nodes[0], originalKey, timeout.Token).ConfigureAwait(true);
        fixture.Context.History.Clear();
        using var host = fixture.CreateInspectorHost(fixture.Source.RootNodes.ToList());
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var button = (SplitButton)await FindInspectorControlAsync(scroller, () => view.FindDescendant<SplitButton>(value => string.Equals(value.Name, "MaterialSplitButton", StringComparison.Ordinal)), "Material", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await PickAssetAsync(button, choice, material: true, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var expected = string.Equals(choice, "New", StringComparison.Ordinal) ? nextKey : defaultKey;
        await AssertMaterialPairAsync(fixture, nodes, expected, expected, timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be(choice);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertMaterialPairAsync(fixture, nodes, originalKey, defaultKey, timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertMaterialPairAsync(fixture, nodes, expected, expected, timeout.Token).ConfigureAwait(true);
        var source = fixture.Source.RootNodes.Select(ReadMaterialUri).ToArray();
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertMaterialPairAsync(fixture, nodes, expected, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.RootNodes.Select(ReadMaterialUri).Should().Equal(source);
    });

    private static Uri? ReadMaterialUri(SceneNode node)
        => node.Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri;

    private static async Task AssertMaterialPairAsync(NativeSceneFixture fixture, Guid[] nodes, string first, string second, CancellationToken cancellationToken)
    {
        await AssertMaterialAsync(fixture, nodes[0], first, cancellationToken).ConfigureAwait(true);
        await AssertMaterialAsync(fixture, nodes[1], second, cancellationToken).ConfigureAwait(true);
    }

    private static async Task AssertMaterialAsync(NativeSceneFixture fixture, Guid nodeId, string expected, CancellationToken cancellationToken)
    {
        var state = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialKeys.Length == 1 && string.Equals(value.MaterialKeys[0], expected, StringComparison.OrdinalIgnoreCase), cancellationToken).ConfigureAwait(true);
        _ = state.MaterialKeys.Should().ContainSingle().Which.Should().BeEquivalentTo(expected);
    }
}
