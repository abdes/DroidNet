// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Defines how to traverse from scope roots.
/// </summary>
public enum AssetQueryTraversal
{
    /// <summary>
    /// Targets all assets in the catalog (ignores roots).
    /// </summary>
    All,

    /// <summary>
    /// Targets only items that match the root(s) directly.
    /// </summary>
    Self,

    /// <summary>
    /// Targets immediate children of the root(s).
    /// </summary>
    Children,

    /// <summary>
    /// Targets all descendants of the root(s) (recursive).
    /// </summary>
    Descendants,
}
