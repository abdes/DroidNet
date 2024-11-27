// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Aura;

/// <summary>
/// Provides functionality to build and manage menus, including menu flyouts and menu bars.
/// </summary>
/// <remarks>
/// The <see cref="MenuBuilder"/> class allows you to create and configure menus for your application. It supports adding menu items, building menu flyouts, and constructing menu bars. The class also maintains a lookup dictionary for quick access to menu items by their identifiers.
/// </remarks>
public class MenuBuilder
{
    private readonly List<MenuItem> menuItems = [];
    private Dictionary<string, MenuItem> menuItemsLookup = [];

    /// <summary>
    /// Tries to get a menu item by its identifier.
    /// </summary>
    /// <param name="id">The identifier of the menu item.</param>
    /// <param name="menuItem">When this method returns, contains the menu item associated with the specified identifier, if the identifier is found; otherwise, the default value for the type of the menuItem parameter.</param>
    /// <returns><see langword="true"/> if the menu item is found; otherwise, <see langword="false"/>.</returns>
    public bool TryGetMenuItemById(string id, out MenuItem menuItem) => this.menuItemsLookup.TryGetValue(id, out menuItem!);

    /// <summary>
    /// Adds a menu item to the builder.
    /// </summary>
    /// <param name="menuItem">The menu item to add.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddMenuItem(MenuItem menuItem)
    {
        this.menuItems.Add(menuItem);
        return this;
    }

    /// <summary>
    /// Builds a <see cref="MenuFlyout"/> from the added menu items.
    /// </summary>
    /// <returns>A <see cref="MenuFlyout"/> containing the configured menu items.</returns>
    public MenuFlyout BuildMenuFlyout()
    {
        var menuFlyout = new MenuFlyout();
        foreach (var menuItem in this.menuItems)
        {
            AddMenuFlyoutItem(menuFlyout.Items, menuItem, menuItem.Command);
        }

        this.BuildLookup();
        return menuFlyout;
    }

    /// <summary>
    /// Builds a <see cref="MenuBar"/> from the added menu items.
    /// </summary>
    /// <returns>A <see cref="MenuBar"/> containing the configured menu items.</returns>
    public MenuBar BuildMenuBar()
    {
        var menuBar = new MenuBar();
        foreach (var menuItem in this.menuItems)
        {
            var menuBarItem = new MenuBarItem { Title = menuItem.Text };
            foreach (var subItem in menuItem.SubItems)
            {
                AddMenuFlyoutItem(menuBarItem.Items, subItem, menuItem.Command);
            }

            menuBar.Items.Add(menuBarItem);
        }

        this.BuildLookup();
        return menuBar;
    }

    /// <summary>
    /// Adds a menu flyout item to the specified collection of items.
    /// </summary>
    /// <param name="items">The collection of items to which the menu flyout item will be added.</param>
    /// <param name="menuItem">The menu item to add.</param>
    /// <param name="parentCommand">The command associated with the parent menu item.</param>
    /// <param name="parentFullId">The full identifier of the parent menu item.</param>
    private static void AddMenuFlyoutItem(IList<MenuFlyoutItemBase> items, MenuItem menuItem, ICommand? parentCommand, string parentFullId = "")
    {
        if (menuItem.SubItems.Any())
        {
            var subMenuFlyoutItem = new MenuFlyoutSubItem { Text = menuItem.Text };
            subMenuFlyoutItem.Items.Clear();
            var subItems = menuItem.SubItems;
            foreach (var subItem in subItems)
            {
                AddMenuFlyoutItem(subMenuFlyoutItem.Items, subItem, menuItem.Command ?? parentCommand, GetFullId(parentFullId, menuItem.Id));
            }

            items.Add(subMenuFlyoutItem);
        }
        else
        {
            var flyoutItem = new MenuFlyoutItem
            {
                Text = menuItem.Text,
                Command = menuItem.Command ?? parentCommand,
                CommandParameter = menuItem.Command == null && parentCommand != null ? GetFullId(parentFullId, menuItem.Id) : null,
            };

            var binding = new Binding
            {
                Path = new PropertyPath("IsSelected"),
                Source = menuItem,
                Mode = BindingMode.OneWay,
                Converter = new IsSelectedToIconConverter(),
            };
            flyoutItem.SetBinding(MenuFlyoutItem.IconProperty, binding);

            items.Add(flyoutItem);
        }
    }

    /// <summary>
    /// Gets all menu items, including sub-items, with their full identifiers.
    /// </summary>
    /// <param name="menuItem">The menu item to process.</param>
    /// <param name="parentId">The identifier of the parent menu item.</param>
    /// <returns>An enumerable collection of key-value pairs, where the key is the full identifier and the value is the menu item.</returns>
    private static IEnumerable<KeyValuePair<string, MenuItem>> GetAllMenuItems(MenuItem menuItem, string parentId)
    {
        yield return new KeyValuePair<string, MenuItem>(parentId, menuItem);
        foreach (var subItem in menuItem.SubItems)
        {
            var subItemId = $"{parentId}.{subItem.Id}";
            foreach (var subPair in GetAllMenuItems(subItem, subItemId))
            {
                yield return subPair;
            }
        }
    }

    /// <summary>
    /// Constructs a full identifier string by combining a parent identifier and a child identifier.
    /// </summary>
    /// <param name="parentFullId">The parent identifier.</param>
    /// <param name="id">The child identifier.</param>
    /// <returns>A combined identifier string.</returns>
    private static string GetFullId(string parentFullId, string id)
        => string.IsNullOrEmpty(parentFullId) ? id : $"{parentFullId}.{id}";

    /// <summary>
    /// Builds the lookup dictionary for quick access to menu items by their identifiers.
    /// </summary>
    private void BuildLookup() => this.menuItemsLookup = this.menuItems
            .SelectMany(menuItem => GetAllMenuItems(menuItem, menuItem.Id))
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase);
}
