// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>What became of an edit.</summary>
public enum SceneEditOutcome
{
    /// <summary>The edit changed the scene and, unless it only changes how the scene is shown, was recorded.</summary>
    Applied = 0,

    /// <summary>A target is missing or locked: nothing changed.</summary>
    Refused = 1,

    /// <summary>The edit does not fit the scene: nothing changed.</summary>
    Invalid = 2,

    /// <summary>The scene was closed or replaced by a reload: nothing changed.</summary>
    Retired = 3,
}
