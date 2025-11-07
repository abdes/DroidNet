// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Event arguments for the <c>TabDragImageRequest</c> event. Use this to provide a lightweight
///     preview image that represents a <see cref="TabItem"/> while it is being dragged.
/// </summary>
/// <remarks>
///     Raised on the UI thread when the dragged tab leaves its originating <see cref="TabStrip"/>
///     (tear-out begins). Handlers should set <see cref="PreviewImage"/> synchronously and return
///     quickly. The <see cref="RequestedSize"/> property indicates the desired logical size (XAML
///     pixels) for the preview image; handlers may honor that size or provide a different scale.
///     The control performs DPI-aware scaling when composing the drag overlay.
/// </remarks>
public sealed class TabDragImageRequestEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the logical <see cref="TabItem"/> being dragged.
    /// </summary>
    /// <value>
    ///     The <see cref="TabItem"/> instance that is the subject of the drag operation. This value
    ///     is provided by the <see cref="TabStrip"/> and is never <see langword="null"/> when the
    ///     event is raised.
    /// </value>
    public TabItem Item { get; init; } = null!;

    /// <summary>
    ///     Gets the requested logical size for the preview image.
    /// </summary>
    /// <value>
    ///     The preferred size (width and height) in XAML logical pixels. Handlers may provide an
    ///     image that matches this size or a different size; the control will perform appropriate
    ///     DPI scaling when rendering the final overlay.
    /// </value>
    public Windows.Foundation.Size RequestedSize { get; init; }

    /// <summary>
    ///     Gets or sets an optional <see cref="ImageSource"/> to display in the drag overlay for
    ///     the dragged tab.
    /// </summary>
    /// <value>
    ///     The image to use as the drag preview. Set this property synchronously in the event
    ///     handler. If not set (<see langword="null"/>), the control renders a compact fallback
    ///     visual.
    /// </value>
    /// <remarks>
    ///     Prefer lightweight, already-decoded image sources (for example, <c>BitmapImage</c> with
    ///     a cached source) to avoid costly decoding work on the UI thread.
    /// </remarks>
    public ImageSource? PreviewImage { get; set; }
}
