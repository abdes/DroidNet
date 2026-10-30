// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>Which file gave a resolved mount its location.</summary>
public enum MountOrigin
{
    /// <summary>Nothing: the mount is unbound.</summary>
    None = 0,

    /// <summary>A <c>.mounts.json</c>, at the project root or above it.</summary>
    MountsFile = 1,

    /// <summary>The relative path committed in <c>Project.oxy</c>.</summary>
    Committed = 2,
}
