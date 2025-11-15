// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

/// <summary>
/// Represents an item that can be nested within a folder structure.
/// </summary>
public interface INestedItem
{
    /// <summary>
    /// Gets the path of the parent folder containing the current item.
    /// </summary>
    public string ParentPath { get; }
}
