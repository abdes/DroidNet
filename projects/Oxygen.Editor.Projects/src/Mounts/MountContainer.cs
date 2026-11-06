// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>How a cooked mount's content is stored, which its binding decides.</summary>
public enum MountContainer
{
    /// <summary>Not a bound cooked mount.</summary>
    None = 0,

    /// <summary>A folder with a loose cooked index.</summary>
    LooseRoot = 1,

    /// <summary>The loose generation a library project's <c>.cooked</c> folder selects for the mount's root.</summary>
    Generation = 2,

    /// <summary>A <c>.pak</c> file.</summary>
    Pak = 3,
}
