// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Drag;

/// <summary>The state of the keys that end a drag, wherever the pointer is.</summary>
/// <param name="IsPrimaryDown">Whether the primary button of the pointer is still down.</param>
/// <param name="IsEscapeDown">Whether Esc is down, which cancels the drag.</param>
internal readonly record struct DragButtons(bool IsPrimaryDown, bool IsEscapeDown);
