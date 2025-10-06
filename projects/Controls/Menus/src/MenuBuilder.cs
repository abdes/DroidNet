// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Provides functionality to build and manage menu data for custom controls.
/// </summary>
/// <remarks>
/// The <see cref="MenuBuilder"/> class creates a hierarchical collection of <see cref="MenuItemData"/> items
/// that can be consumed by custom menu containers. It maintains a lookup dictionary for quick access to menu
/// items by their identifiers and exposes a reusable <see cref="IMenuSource"/> pairing the items with shared
/// <see cref="MenuServices"/> helpers.
/// </remarks>
public class MenuBuilder
{
    private readonly List<MenuItemData> menuItems = [];
    private Dictionary<string, MenuItemData> menuItemsLookup = [];
    private ObservableCollection<MenuItemData>? observableItems;
    private MenuSource? menuSource;
    private MenuServices? menuServices;
    private bool isLookupDirty = true;

    /// <summary>
    /// Initializes a new instance of the <see cref="MenuBuilder"/> class.
    /// </summary>
    public MenuBuilder()
    {
    }

    /// <summary>
    /// Gets the collection of root menu items for direct binding to custom controls.
    /// </summary>
    public IReadOnlyList<MenuItemData> MenuItems => this.menuItems.AsReadOnly();

    /// <summary>
    /// Builds the menu definition and returns an <see cref="IMenuSource"/> for binding.
    /// </summary>
    /// <returns>A reusable <see cref="IMenuSource"/> containing the menu items and services.</returns>
    public IMenuSource Build()
    {
        this.RebuildIdentifiers();
        this.observableItems ??= new ObservableCollection<MenuItemData>(this.menuItems);
        this.EnsureLookup();

        this.menuServices ??= new MenuServices(this.GetLookupSnapshot, this.HandleGroupSelection);
        this.menuSource ??= new MenuSource(this.observableItems, this.menuServices);
        return this.menuSource;
    }

    /// <summary>
    /// Adds a menu item to the builder.
    /// </summary>
    /// <param name="menuItem">The menu item to add.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddMenuItem(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        MenuBuilderHelpers.RealizeSubtree(menuItem);

        this.menuItems.Add(menuItem);
        this.observableItems?.Add(menuItem);
        this.RebuildIdentifiers();
        return this;
    }

    /// <summary>
    /// Fluent API: Adds a menu item with text and command.
    /// </summary>
    /// <param name="text">The menu item text.</param>
    /// <param name="command">The command to execute when selected.</param>
    /// <param name="icon">Optional icon for the menu item.</param>
    /// <param name="acceleratorText">Optional accelerator text (e.g., "Ctrl+S").</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddMenuItem(string text, ICommand? command = null, IconSource? icon = null, string? acceleratorText = null)
    {
        var item = new MenuItemData
        {
            Text = text,
            Command = command,
            Icon = icon,
            AcceleratorText = acceleratorText,
        };
        return this.AddMenuItem(item);
    }

    /// <summary>
    /// Fluent API: Adds a submenu with child items configured via action.
    /// </summary>
    /// <param name="text">The submenu text.</param>
    /// <param name="configureSubmenu">Action to configure the submenu items.</param>
    /// <param name="icon">Optional icon for the submenu.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddSubmenu(string text, Action<MenuBuilder> configureSubmenu, IconSource? icon = null)
    {
        var submenuBuilder = new MenuBuilder();
        configureSubmenu(submenuBuilder);

        var item = new MenuItemData
        {
            Text = text,
            Icon = icon,
            SubItems = submenuBuilder.MenuItems,
        };
        return this.AddMenuItem(item);
    }

    /// <summary>
    /// Fluent API: Adds a checkable menu item.
    /// </summary>
    /// <param name="text">The menu item text.</param>
    /// <param name="isChecked">Initial checked (toggle) state.</param>
    /// <param name="command">The command to execute when toggled.</param>
    /// <param name="icon">Optional icon for the menu item.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddCheckableMenuItem(string text, bool isChecked = false, ICommand? command = null, IconSource? icon = null)
    {
        var item = new MenuItemData
        {
            Text = text,
            IsCheckable = true,
            IsChecked = isChecked,
            Command = command,
            Icon = icon,
        };
        return this.AddMenuItem(item);
    }

    /// <summary>
    /// Fluent API: Adds a menu item to a radio group.
    /// </summary>
    /// <param name="text">The menu item text.</param>
    /// <param name="radioGroupId">The radio group identifier.</param>
    /// <param name="isChecked">Whether this item is the initially checked item in the group.</param>
    /// <param name="command">The command to execute when selected.</param>
    /// <param name="icon">Optional icon for the menu item.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddRadioMenuItem(string text, string radioGroupId, bool isChecked = false, ICommand? command = null, IconSource? icon = null)
    {
        var item = new MenuItemData
        {
            Text = text,
            RadioGroupId = radioGroupId,
            IsChecked = isChecked,
            Command = command,
            Icon = icon,
        };
        return this.AddMenuItem(item);
    }

    /// <summary>
    /// Fluent API: Adds a separator.
    /// </summary>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddSeparator()
    {
        var item = new MenuItemData { IsSeparator = true };
        return this.AddMenuItem(item);
    }

    private IEnumerable<MenuItemData> EnumerateAllItems() => this.menuItems.SelectMany(MenuBuilderHelpers.EnumerateSubtree);

    private void HandleGroupSelection(MenuItemData selectedItem)
    {
        ArgumentNullException.ThrowIfNull(selectedItem);

        if (string.IsNullOrEmpty(selectedItem.RadioGroupId))
        {
            // Not a grouped item - just toggle if checkable
            if (selectedItem.IsCheckable)
            {
                selectedItem.IsChecked = !selectedItem.IsChecked;
            }

            return;
        }

        // Grouped item - unselect others in same group
        var groupItems = this.EnumerateAllItems().Where(item => string.Equals(item.RadioGroupId, selectedItem.RadioGroupId, StringComparison.Ordinal));
        foreach (var item in groupItems)
        {
            item.IsChecked = false;
        }

        selectedItem.IsChecked = true;
    }

    private void MarkLookupDirty() => this.isLookupDirty = true;

    private void RebuildIdentifiers()
    {
        var rootIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var rootItem in this.menuItems)
        {
            var baseId = MenuBuilderHelpers.NormalizeIdSegment(rootItem.Text);
            var uniqueRootId = MenuBuilderHelpers.EnsureUniqueId(baseId, rootIds);
            rootIds.Add(uniqueRootId);
            MenuBuilderHelpers.ApplyHierarchyIdentifiers(rootItem, uniqueRootId);
        }

        this.MarkLookupDirty();
    }

    private void EnsureLookup()
    {
        if (!this.isLookupDirty)
        {
            return;
        }

        this.menuItemsLookup = this.menuItems
            .SelectMany(menuItem => MenuBuilderHelpers.EnumerateSubtreeWithIdentifiers(menuItem, menuItem.Id))
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase);

        this.isLookupDirty = false;
    }

    private Dictionary<string, MenuItemData> GetLookupSnapshot()
    {
        this.EnsureLookup();
        return this.menuItemsLookup;
    }

    private static class MenuBuilderHelpers
    {
        public static IEnumerable<MenuItemData> EnumerateSubtree(MenuItemData menuItem)
        {
            yield return menuItem;
            foreach (var subItem in menuItem.SubItems)
            {
                foreach (var nested in EnumerateSubtree(subItem))
                {
                    yield return nested;
                }
            }
        }

        public static IEnumerable<KeyValuePair<string, MenuItemData>> EnumerateSubtreeWithIdentifiers(MenuItemData menuItem, string currentId)
        {
            var effectiveId = string.IsNullOrWhiteSpace(currentId) ? NormalizeIdSegment(menuItem.Text) : currentId;
            yield return new KeyValuePair<string, MenuItemData>(effectiveId, menuItem);

            foreach (var subItem in menuItem.SubItems)
            {
                var childId = string.IsNullOrWhiteSpace(subItem.Id)
                    ? string.IsNullOrWhiteSpace(effectiveId)
                        ? NormalizeIdSegment(subItem.Text)
                        : $"{effectiveId}.{NormalizeIdSegment(subItem.Text)}"
                    : subItem.Id;

                foreach (var pair in EnumerateSubtreeWithIdentifiers(subItem, childId))
                {
                    yield return pair;
                }
            }
        }

        public static string NormalizeIdSegment(string text)
        {
            if (string.IsNullOrWhiteSpace(text))
            {
                return "ITEM";
            }

            // Normalize text for ID generation
            var normalized = text.Replace(' ', '_').Replace('.', '_').Replace('-', '_');

            // Keep only letters, numbers, underscores, and ampersands
            var result = new System.Text.StringBuilder();
            foreach (var c in normalized)
            {
                if (char.IsLetterOrDigit(c) || c == '_' || c == '&')
                {
                    result.Append(char.ToUpperInvariant(c));
                }
            }

            return result.Length > 0 ? result.ToString() : "ITEM";
        }

        public static string EnsureUniqueId(string baseId, IEnumerable<string> existingIds)
        {
            var existingSet = new HashSet<string>(existingIds, StringComparer.OrdinalIgnoreCase);

            if (!existingSet.Contains(baseId))
            {
                return baseId;
            }

            var counter = 1;
            string uniqueId;
            do
            {
                uniqueId = string.Create(CultureInfo.InvariantCulture, $"{baseId}_{counter}");
                counter++;
            }
            while (existingSet.Contains(uniqueId));

            return uniqueId;
        }

        public static void ApplyHierarchyIdentifiers(MenuItemData menuItem, string parentId)
        {
            menuItem.Id = parentId;

            if (!menuItem.SubItems.Any())
            {
                return;
            }

            var subItemIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var subItem in menuItem.SubItems)
            {
                // Ensure we use the text, not any existing ID
                var subItemBaseId = NormalizeIdSegment(subItem.Text);
                var subItemId = EnsureUniqueId(subItemBaseId, subItemIds);
                var fullSubItemId = $"{parentId}.{subItemId}";

                subItemIds.Add(subItemId);
                ApplyHierarchyIdentifiers(subItem, fullSubItemId);
            }
        }

        public static void RealizeSubtree(MenuItemData menuItem)
        {
            ArgumentNullException.ThrowIfNull(menuItem);

            var realized = menuItem.SubItems?.ToList() ?? [];
            menuItem.SubItems = realized;

            foreach (var subItem in realized)
            {
                RealizeSubtree(subItem);
            }
        }
    }
}
