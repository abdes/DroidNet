// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>An operation that reads saved files, and so saves the unsaved documents it needs before it runs.</summary>
public enum SaveBeforeOperation
{
    /// <summary>Play: cooks what the scene needs and starts the player.</summary>
    Play,

    /// <summary>An explicit cook.</summary>
    Cook,

    /// <summary>A rename of assets, which rewrites the files that use them.</summary>
    Rename,

    /// <summary>A move of assets, which rewrites the files that use them.</summary>
    Move,
}
