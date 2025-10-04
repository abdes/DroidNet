// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;

namespace DroidNet.Controls;

/// <summary>
/// Provides lookup, grouping, and interaction helpers for menu controls built on top of <see cref="MenuItemData"/> collections.
/// </summary>
public sealed class MenuServices
{
    private readonly Func<IReadOnlyDictionary<string, MenuItemData>> lookupAccessor;
    private readonly Action<MenuItemData> groupSelectionHandler;
    private readonly Lock controllerGate = new();
    private MenuInteractionController? interactionController;

    /// <summary>
    /// Initializes a new instance of the <see cref="MenuServices"/> class.
    /// </summary>
    /// <param name="lookupAccessor">A delegate that returns the latest lookup dictionary by menu item identifier.</param>
    /// <param name="groupSelectionHandler">A delegate that applies radio-group and toggle selection logic for a given item.</param>
    internal MenuServices(
        Func<IReadOnlyDictionary<string, MenuItemData>> lookupAccessor,
        Action<MenuItemData> groupSelectionHandler)
    {
        ArgumentNullException.ThrowIfNull(lookupAccessor);
        ArgumentNullException.ThrowIfNull(groupSelectionHandler);

        this.lookupAccessor = lookupAccessor;
        this.groupSelectionHandler = groupSelectionHandler;
    }

    /// <summary>
    /// Gets the shared <see cref="MenuInteractionController"/> coordinating menu interactions.
    /// </summary>
    public MenuInteractionController InteractionController
    {
        get
        {
            if (this.interactionController is { })
            {
                return this.interactionController;
            }

            lock (this.controllerGate)
            {
                this.interactionController ??= new MenuInteractionController(this);
                return this.interactionController;
            }
        }
    }

    /// <summary>
    /// Attempts to find a menu item by its hierarchical identifier.
    /// </summary>
    /// <param name="id">The menu item identifier.</param>
    /// <param name="menuItem">The located menu item when the lookup succeeds, otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when a menu item with the supplied identifier exists; otherwise <see langword="false"/>.</returns>
    public bool TryGetMenuItemById(string id, out MenuItemData? menuItem)
    {
        menuItem = null;
        if (string.IsNullOrWhiteSpace(id))
        {
            return false;
        }

        var lookup = this.lookupAccessor();
        if (lookup.TryGetValue(id, out var found))
        {
            menuItem = found;
            return true;
        }

        return false;
    }

    /// <summary>
    /// Provides direct access to the latest lookup dictionary for advanced scenarios.
    /// </summary>
    /// <returns>The lookup dictionary keyed by hierarchical menu item identifiers.</returns>
    public IReadOnlyDictionary<string, MenuItemData> GetLookup() => this.lookupAccessor();

    /// <summary>
    /// Applies menu selection logic, including radio-group coordination, to the supplied menu item.
    /// </summary>
    /// <param name="menuItem">The menu item that was activated.</param>
    public void HandleGroupSelection(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        this.groupSelectionHandler(menuItem);
    }
}
