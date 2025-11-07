// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Event arguments for the <c>TabDragComplete</c> event which signals the completion of a tab
///     drag operation and describes the eventual destination of the dragged <see cref="TabItem"/>.
/// </summary>
/// <remarks>
///     Raised on the UI thread when the control has finished placement and cleanup for the drag
///     operation. Handlers may inspect <see cref="DestinationStrip"/> and <see cref="NewIndex"/>
///     to determine whether the drop succeeded and perform post-drop work (for example, focusing
///     content or persisting state). Handlers should be fast and avoid long-running work on the UI
///     thread.
/// </remarks>
public sealed class TabDragCompleteEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the logical <see cref="TabItem"/> that participated in the completed drag
    ///     operation.
    /// </summary>
    /// <value>
    ///     The <see cref="TabItem"/> that was dragged. This value is provided by the control and is
    ///     non-null when the event is raised.
    /// </value>
    public TabItem Item { get; init; } = null!;

    /// <summary>
    ///     Gets the destination <see cref="TabStrip"/> that contains the item after the drop, or
    ///     <see langword="null"/> if the drop failed.
    /// </summary>
    /// <value>
    ///     The destination <see cref="TabStrip"/>, or <see langword="null"/> when the drop failed.
    /// </value>
    public TabStrip? DestinationStrip { get; init; }

    /// <summary>
    ///     Gets the zero-based index within <see cref="DestinationStrip"/> where the item was
    ///     inserted as a result of the drop, or <see langword="null"/> if the drop failed.
    /// </summary>
    /// <value>
    ///     The new index in the destination strip, or <see langword="null"/> when the item is not
    ///     part of any <see cref="TabStrip"/> after the drag.
    /// </value>
    public int? NewIndex { get; init; }
}
