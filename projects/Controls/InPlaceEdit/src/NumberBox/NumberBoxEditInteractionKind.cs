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
    ///     Pointer drag on a visible label.
    /// </summary>
    PointerDrag,

    /// <summary>
    ///     Legacy wheel interaction identifier. NumberBox does not emit it for wheel input.
    /// </summary>
    MouseWheel,
}
