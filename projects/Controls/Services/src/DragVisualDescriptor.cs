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
    /// <summary>
    ///     Gets or sets the header image to display in the drag overlay (required).
    /// </summary>
    [ObservableProperty]
    public partial ImageSource? HeaderImage { get; set; }

    /// <summary>
    ///     Gets or sets the optional preview image to display in the drag overlay.
    /// </summary>
    [ObservableProperty]
    public partial ImageSource? PreviewImage { get; set; }

    /// <summary>
    ///     Gets or sets the requested size for the overlay.
    /// </summary>
    [ObservableProperty]
    public partial Windows.Foundation.Size RequestedSize { get; set; }

    /// <summary>
    ///     Gets or sets an optional title string displayed in the overlay.
    /// </summary>
    [ObservableProperty]
    public partial string? Title { get; set; }
}
