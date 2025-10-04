// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.ObjectModel;
using System.Linq;

namespace DroidNet.Controls;

/// <summary>
///     Lightweight <see cref="IMenuSource"/> implementation that projects a subset of menu items while
///     reusing the shared <see cref="MenuServices"/> instance from the root menu definition.
/// </summary>
internal sealed class MenuSourceView : IMenuSource
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuSourceView"/> class.
    /// </summary>
    /// <param name="items">The items that compose the view.</param>
    /// <param name="services">The shared services instance.</param>
    public MenuSourceView(System.Collections.Generic.IEnumerable<MenuItemData> items, MenuServices services)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(services);

        this.Items = new ObservableCollection<MenuItemData>(items.ToList());
        this.Services = services;
    }

    /// <inheritdoc />
    public ObservableCollection<MenuItemData> Items { get; }

    /// <inheritdoc />
    public MenuServices Services { get; }
}
