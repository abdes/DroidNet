// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Changes;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>What applying an edit did.</summary>
public abstract record SceneEditApplication
{
    private SceneEditApplication()
    {
    }

    /// <summary>The edit applied completely.</summary>
    /// <param name="Revert">The edit that restores exactly the state this one replaced; its own revert is the redo.</param>
    /// <param name="Changes">What the edit changed, in order.</param>
    public sealed record Applied(ISceneEdit Revert, IReadOnlyList<SceneChange> Changes) : SceneEditApplication;

    /// <summary>A composite edit stopped part way and put the scene back as it found it.</summary>
    /// <param name="Validation">Why the part that stopped it failed.</param>
    public sealed record Rejected(SceneEditValidation Validation) : SceneEditApplication;
}
