// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

/// <summary>
/// A set of flags to specify the kind of storage items that should be included
/// when enumerating items in a folder.
/// </summary>
[Flags]
public enum ProjectItemKind
{
    /// <summary>Special empty value primarily used to test for when no flag is set.</summary>
    None = 0,

    /// <summary>A folder.</summary>
    Folder = 1 << 0,

    /// <summary>A file.</summary>
    File = 1 << 1,

    /// <summary>Special value that includes all items.</summary>
    All = File | Folder,
}
