// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.ObjectModel;

namespace DroidNet.Controls;

/// <summary>
/// Default implementation of <see cref="IMenuSource"/> that pairs an items collection with shared menu services.
/// </summary>
public sealed class MenuSource : IMenuSource
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuSource"/> class.
    /// </summary>
    /// <param name="items">The observable collection of menu items.</param>
    /// <param name="services">The services instance coordinating lookups and selection.</param>
    public MenuSource(ObservableCollection<MenuItemData> items, MenuServices services)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(services);

        this.Items = items;
        this.Services = services;
    }

    /// <inheritdoc />
    public ObservableCollection<MenuItemData> Items { get; }

    /// <inheritdoc />
    public MenuServices Services { get; }
}
