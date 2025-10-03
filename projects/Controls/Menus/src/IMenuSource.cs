// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Controls;

/// <summary>
/// Represents a data-first menu source that exposes the items and supporting services required by custom menu controls.
/// </summary>
public interface IMenuSource
{
    /// <summary>
    /// Gets the observable collection of top-level menu items.
    /// </summary>
    public ObservableCollection<MenuItemData> Items { get; }

    /// <summary>
    /// Gets the menu services that provide lookup and coordination helpers for the menu system.
    /// </summary>
    public MenuServices Services { get; }
}
