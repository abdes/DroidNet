// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Drag;

/// <summary>
///     Represents the minimal drag/drag-drop phases used by the TabStrip drag coordinator. Mirrors
///     the states from the design review: Ready, Reordering, Detached and Attached.
/// </summary>
public enum DragState
{
    /// <summary>
    ///     Item is attached to a strip and its container is realized. Can start reordering.
    /// </summary>
    Ready = 0,

    /// <summary>
    ///     Actively dragging within a strip (reordering in progress).
    /// </summary>
    Reordering = 1,

    /// <summary>
    ///     Item has been torn out and is floating (not in any strip's collection).
    /// </summary>
    Detached = 2,

    /// <summary>
    ///     The clone (or moved item) has been inserted into a strip's Items collection but the UI
    ///     has not yet realized a container for it. This is a WAIT state until realization.
    /// </summary>
    Attached = 3,
}
