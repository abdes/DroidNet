// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Windows.Graphics.Imaging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Observable descriptor for drag visual overlay content. Instances are observed by the <see
///     cref="IDragVisualService"/> implementation to update the overlay UI.
/// </summary>
public sealed partial class DragVisualDescriptor : ObservableObject
{
    /// <summary>
    ///     Gets or sets the header bitmap to display in the drag overlay (required).
    /// </summary>
    [ObservableProperty]
    public partial SoftwareBitmap? HeaderBitmap { get; set; }

    /// <summary>
    ///     Gets or sets the optional preview bitmap to display in the drag overlay.
    /// </summary>
    [ObservableProperty]
    public partial SoftwareBitmap? PreviewBitmap { get; set; }

    /// <summary>
    ///     Gets or sets the requested size (in DIPs) for the overlay.
    /// </summary>
    [ObservableProperty]
    public partial Windows.Foundation.Size RequestedSize { get; set; }

    /// <summary>
    ///     Disposes the current header bitmap when a new instance is assigned.
    /// </summary>
    /// <param name="value">The incoming bitmap.</param>
    partial void OnHeaderBitmapChanging(SoftwareBitmap? value)
    {
        if (this.HeaderBitmap is SoftwareBitmap current && !ReferenceEquals(current, value))
        {
            current.Dispose();
        }
    }

    /// <summary>
    ///     Disposes the current preview bitmap when a new instance is assigned.
    /// </summary>
    /// <param name="value">The incoming bitmap.</param>
    partial void OnPreviewBitmapChanging(SoftwareBitmap? value)
    {
        if (this.PreviewBitmap is SoftwareBitmap current && !ReferenceEquals(current, value))
        {
            current.Dispose();
        }
    }
}
