// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Input interaction that produced a numeric edit.
/// </summary>
public enum NumberBoxEditInteractionKind
{
    /// <summary>
    ///     Text entry using the in-place editor.
    /// </summary>
    Text,

    /// <summary>
    ///     Pointer drag on the numeric value.
    /// </summary>
    PointerDrag,

    /// <summary>
    ///     Mouse wheel increment/decrement.
    /// </summary>
    MouseWheel,
}
