// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="DynamicTreeViewModel.ItemRemoved" /> event.
/// </summary>
public class TreeItemRemovedEventArgs : DynamicTreeEventArgs
{
    public required ITreeItem Parent { get; init; }

    public required int RelativeIndex { get; init; }
}
