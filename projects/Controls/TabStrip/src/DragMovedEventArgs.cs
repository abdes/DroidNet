// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Controls;

    /// <summary>
    /// Event args for drag move notifications.
    /// </summary>
    public sealed class DragMovedEventArgs : EventArgs
    {
        /// <summary>
        /// Gets the screen point reported by the coordinator.
        /// </summary>
        public Windows.Foundation.Point ScreenPoint { get; init; }
    }
