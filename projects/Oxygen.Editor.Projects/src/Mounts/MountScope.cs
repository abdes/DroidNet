// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>Whether a resolved mount is declared by the project or added by a <c>.mounts.json</c>.</summary>
public enum MountScope
{
    /// <summary><c>Project.oxy</c> declares the mount.</summary>
    Declared = 0,

    /// <summary>Only a <c>.mounts.json</c> adds the mount: a source mount of this machine.</summary>
    Local = 1,
}
