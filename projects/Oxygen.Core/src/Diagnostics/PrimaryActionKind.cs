// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Built-in operation result primary action kinds.
/// </summary>
public enum PrimaryActionKind
{
    /// <summary>
    /// Retry the failed operation.
    /// </summary>
    Retry,

    /// <summary>
    /// Remove an invalid or stale item.
    /// </summary>
    Remove,

    /// <summary>
    /// Browse to choose a replacement path or item.
    /// </summary>
    Browse,

    /// <summary>
    /// Open inline or panel details.
    /// </summary>
    OpenDetails,

    /// <summary>
    /// Feature-owned action kind.
    /// </summary>
    Custom,
}
