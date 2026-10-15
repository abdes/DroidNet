// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Scripts;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// The problems a running scene reports, which the Problems panel lists beside the analyzer's.
/// </summary>
public interface IRuntimeProblemSource
{
    /// <summary>Occurs when the problems change; they clear when a session starts.</summary>
    public event EventHandler? RuntimeProblemsChanged;

    /// <summary>Gets the problems of the current session, newest last.</summary>
    public IReadOnlyList<ScriptProblem> RuntimeProblems { get; }

    /// <summary>Selects, in the scene that played, the node a runtime problem names.</summary>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>Whether the node was selected.</returns>
    public Task<bool> SelectProblemNodeAsync(ScriptProblem problem);
}
