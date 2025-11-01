// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Controls;

    /// <summary>
    /// Event args for drag end notifications.
    /// </summary>
    public sealed class DragEndedEventArgs : EventArgs
    {
        /// <summary>
        /// Gets the screen point where the drop occurred.
        /// </summary>
        public Windows.Foundation.Point ScreenPoint { get; init; }

        /// <summary>
        /// Gets a value indicating whether the drop occurred over a TabStrip.
        /// </summary>
        public bool DroppedOverStrip { get; init; }

        /// <summary>
        /// Gets the destination TabStrip if applicable; otherwise null.
        /// </summary>
        public TabStrip? Destination { get; init; }

        /// <summary>
        /// Gets the index at which the item was inserted into the destination, or null.
        /// </summary>
        public int? NewIndex { get; init; }
    }
