// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents the current state of the in-memory clipboard for the dynamic tree.
/// </summary>
public enum ClipboardState
{
    /// <summary>
    ///     No clipboard content is currently tracked.
    /// </summary>
    Empty,

    /// <summary>
    ///     Items were copied and can be pasted as clones.
    /// </summary>
    Copied,

    /// <summary>
    ///     Items were cut and will be moved on paste.
    /// </summary>
    Cut,
}
