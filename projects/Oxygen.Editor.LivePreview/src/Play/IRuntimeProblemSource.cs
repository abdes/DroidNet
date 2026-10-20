// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// The problems a running scene reports, each a <see cref="RuntimeScriptProblem"/>; they clear when a session starts.
/// </summary>
/// <remarks>Its events are raised on the thread that owns the play session, the UI thread.</remarks>
public interface IRuntimeProblemSource : IAssetProblemSource
{
    /// <summary>Finds, in the scene that played, the node a runtime problem names.</summary>
    /// <remarks>The player names nodes, not identities, so only a name that one node has is found.</remarks>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>The scene document and the node, or <see langword="null"/> when no single node has the name.</returns>
    public (DocumentId Document, NodeId Node)? NodeOf(RuntimeScriptProblem problem);
}
