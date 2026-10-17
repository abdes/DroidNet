// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// The problems a running scene reports, which the Problems panel lists beside the analyzer's.
/// </summary>
public interface IRuntimeProblemSource
{
    /// <summary>Occurs when the problems change; they clear when a session starts.</summary>
    public event EventHandler? RuntimeProblemsChanged;

    /// <summary>Gets the problems of the current session, newest last.</summary>
    public IReadOnlyList<ScriptProblem> RuntimeProblems { get; }

    /// <summary>Finds, in the scene that played, the node a runtime problem names.</summary>
    /// <remarks>The player names nodes, not identities, so only a name that one node has is found.</remarks>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>The scene document and the node, or <see langword="null"/> when no single node has the name.</returns>
    public (DocumentId Document, NodeId Node)? NodeOf(ScriptProblem problem);
}
