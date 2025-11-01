// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
///     Observable descriptor for drag visual overlay content. Instances are observed by the <see
///     cref="IDragVisualService"/> implementation to update the overlay UI.
/// </summary>
public sealed partial class DragVisualDescriptor : ObservableObject
{
    private ImageSource? headerImage;
    private ImageSource? previewImage;
    private Windows.Foundation.Size requestedSize;
    private string? title;

    /// <summary>
    ///     Gets or sets the header image to display in the drag overlay (required).
    /// </summary>
    public ImageSource? HeaderImage
    {
        get => this.headerImage;
        set => this.SetProperty(ref this.headerImage, value);
    }

    /// <summary>
    ///     Gets or sets the optional preview image to display in the drag overlay.
    /// </summary>
    public ImageSource? PreviewImage
    {
        get => this.previewImage;
        set => this.SetProperty(ref this.previewImage, value);
    }

    /// <summary>
    ///     Gets or sets the requested size for the overlay.
    /// </summary>
    public Windows.Foundation.Size RequestedSize
    {
        get => this.requestedSize;
        set => this.SetProperty(ref this.requestedSize, value);
    }

    /// <summary>
    ///     Gets or sets an optional title string displayed in the overlay.
    /// </summary>
    public string? Title
    {
        get => this.title;
        set => this.SetProperty(ref this.title, value);
    }
}
