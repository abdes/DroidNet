// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Controls;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>The Explorer's clipboard state; what a paste does to the scene is <c>SceneClipboard</c>'s to test.</summary>
public sealed partial class SceneExplorerAuthoringTests
{
    private static readonly JsonSerializerOptions ClipboardJsonOptions = new() { IncludeFields = true };

    [TestMethod]
    public async Task CutItemsAsync_EscapeClearClipboardIntent_CancelsMarksWithoutAuthoring()
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();
        var before = JsonSerializer.Serialize(scene.Dehydrate(), ClipboardJsonOptions);

        await explorer.CutItemsAsync([adapter!]).ConfigureAwait(false);

        _ = adapter!.IsCut.Should().BeTrue();
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = explorer.UndoStack.Should().BeEmpty();

        // DynamicTree's Escape handler dispatches this intent; no workstation keyboard injection is needed.
        await explorer.ClearClipboardAsync().ConfigureAwait(false);
        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);

        _ = adapter.IsCut.Should().BeFalse();
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = explorer.PasteCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = JsonSerializer.Serialize(scene.Dehydrate(), ClipboardJsonOptions).Should().Be(before);
        _ = explorer.UndoStack.Should().BeEmpty();
        _ = explorer.RedoStack.Should().BeEmpty();
        _ = harness.Documents.Documents.Single().IsDirty.Should().BeFalse();
    }
}
