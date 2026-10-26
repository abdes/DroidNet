// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Problems;

/// <summary>
/// Shows the scene document a runtime problem names in the window's document area and selects the node in it through
/// the scene selection.
/// </summary>
/// <param name="runtime">The runtime problems, which find the node a problem names.</param>
/// <param name="area">The window's document area.</param>
/// <param name="selection">The scene selection every panel follows.</param>
public sealed class RuntimeProblemNodes(IRuntimeProblemSource runtime, IDocumentArea area, ISceneSelectionService selection) : IRuntimeProblemNodes
{
    /// <summary>The source the selection names.</summary>
    public const string SelectionSource = "Problems";

    /// <inheritdoc />
    public async Task<bool> SelectAsync(RuntimeScriptProblem problem)
    {
        ArgumentNullException.ThrowIfNull(problem);
        if (runtime.NodeOf(problem) is not { } target
            || area.Documents.FirstOrDefault(document => document.Id == target.Document) is not { Content: SceneContent content } document
            || !await area.ShowAsync(document).ConfigureAwait(true)
            || content.Scene.AllNodes.FirstOrDefault(node => node.Id == target.Node) is not { } node)
        {
            return false;
        }

        selection.SetSelection(target.Document, [NodeReference.To(node.Id)], SelectionSource);
        return true;
    }
}
