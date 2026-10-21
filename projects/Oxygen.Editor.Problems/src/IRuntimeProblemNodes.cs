// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.LivePreview.Play;

namespace Oxygen.Editor.Problems;

/// <summary>Shows the node of the scene that played, which a runtime problem names.</summary>
public interface IRuntimeProblemNodes
{
    /// <summary>Shows the scene document a runtime problem names and selects its node.</summary>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>Whether the node was selected.</returns>
    public Task<bool> SelectAsync(RuntimeScriptProblem problem);
}
