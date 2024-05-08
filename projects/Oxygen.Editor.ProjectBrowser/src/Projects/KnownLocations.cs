// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Storage;

public enum KnownLocations
{
    /// <summary>A location which children are the recently used projects.</summary>
    RecentProjects = 0,

    /// <summary>A location which children are the local drives on this computer.</summary>
    ThisComputer = 1,

    /// <summary>Root of the OneDrive online storage if the user has one.</summary>
    OneDrive = 2,

    /// <summary>The user Downloads  folder.</summary>
    Downloads = 3,

    /// <summary>The user documents local folder.</summary>
    Documents = 4,

    /// <summary>The user Desktop folder.</summary>
    Desktop = 5,
}
