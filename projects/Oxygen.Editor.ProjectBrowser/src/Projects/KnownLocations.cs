// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Storage;

public enum KnownLocations
{
    /// <summary>A location which children are the recently used projects.</summary>
    RecentProjects,

    /// <summary>A location which children are the local drives on this computer.</summary>
    ThisComputer,

    /// <summary>Root of the OneDrive online storage if the user has one.</summary>
    OneDrive,

    /// <summary>The user Downloads  folder.</summary>
    Downloads,

    /// <summary>The user documents local folder.</summary>
    Documents,

    /// <summary>The user Desktop folder.</summary>
    Desktop,
}
