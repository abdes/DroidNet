// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>Whether a project owns a mount's folder.</summary>
public enum MountRole
{
    /// <summary>The folder lies outside the project, or the mount is cooked.</summary>
    Shared = 0,

    /// <summary>A source folder inside the project root.</summary>
    Own = 1,
}
