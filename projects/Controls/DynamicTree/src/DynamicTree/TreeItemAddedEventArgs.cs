// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="DynamicTreeViewModel.ItemAdded" /> event.
/// </summary>
public class TreeItemAddedEventArgs : DynamicTreeEventArgs
{
    public required int RelativeIndex { get; init; }
}
