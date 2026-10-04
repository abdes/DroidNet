// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>Identifies the generic tree item and visual anchor for a context-menu request.</summary>
/// <param name="item">The requested logical item.</param>
/// <param name="anchor">The realized row that received the context request.</param>
/// <param name="position">The pointer position in anchor coordinates, or null for keyboard invocation.</param>
public sealed class TreeItemContextRequestedEventArgs(ITreeItem item, FrameworkElement anchor, Point? position) : EventArgs
{
    /// <summary>Gets the requested logical item.</summary>
    public ITreeItem Item { get; } = item;

    /// <summary>Gets the realized row receiving the request.</summary>
    public FrameworkElement Anchor { get; } = anchor;

    /// <summary>Gets the pointer position, or null for a keyboard request.</summary>
    public Point? Position { get; } = position;
}
