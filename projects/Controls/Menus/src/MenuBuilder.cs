// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls;

/// <summary>
/// Provides functionality to build and manage menus, including menu flyouts and menu bars.
/// </summary>
/// <remarks>
/// The <see cref="MenuBuilder"/> class allows you to create and configure menus for your application. It supports adding menu items, building menu flyouts, and constructing menu bars. The class also maintains a lookup dictionary for quick access to menu items by their identifiers.
/// </remarks>
public class MenuBuilder
{
    private readonly bool assignIds;
    private readonly List<MenuItemData> menuItems = [];
    private Dictionary<string, MenuItemData> menuItemsLookup = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="MenuBuilder"/> class.
    /// </summary>
    /// <param name="assignIds">Whether to assign hierarchical IDs to menu items.</param>
    public MenuBuilder(bool assignIds = true)
    {
        this.assignIds = assignIds;
    }

    /// <summary>
    /// Gets the collection of root menu items for direct binding to custom controls.
    /// </summary>
    public IReadOnlyList<MenuItemData> MenuItems => this.menuItems.AsReadOnly();

    /// <summary>
    /// Gets all menu items including sub-items, with their full identifiers.
    /// Useful for group selection logic and global menu item operations.
    /// </summary>
    public IEnumerable<MenuItemData> AllItems => this.menuItems.SelectMany(item => GetAllMenuItemsFlat(item));

    /// <summary>
    /// Tries to get a menu item by its identifier.
    /// </summary>
    /// <param name="id">The identifier of the menu item.</param>
    /// <param name="menuItem">When this method returns, contains the menu item associated with the specified identifier, if the identifier is found; otherwise, the default value for the type of the menuItem parameter.</param>
    /// <returns><see langword="true"/> if the menu item is found; otherwise, <see langword="false"/>.</returns>
    public bool TryGetMenuItemById(string id, out MenuItemData menuItem) => this.menuItemsLookup.TryGetValue(id, out menuItem!);

    /// <summary>
    /// Adds a menu item to the builder.
    /// </summary>
    /// <param name="menuItem">The menu item to add.</param>
    /// <returns>The current <see cref="MenuBuilder"/> instance.</returns>
    public MenuBuilder AddMenuItem(MenuItemData menuItem)
    {
        if (this.assignIds)
        {
            // Assign hierarchical ID based on position and text
            var baseId = NormalizeTextForId(menuItem.Text);
            var rootId = EnsureUniqueId(baseId, this.menuItems.Select(item => item.Id));
            AssignHierarchicalIds(menuItem, rootId);
        }

        this.menuItems.Add(menuItem);
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
        var submenuBuilder = new MenuBuilder(assignIds: false);
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

    /// <summary>
    /// Handles radio group selection logic for custom controls.
    /// When a grouped item is selected, unselects other items in the same group.
    /// </summary>
    /// <param name="selectedItem">The item that was selected.</param>
    public void HandleGroupSelection(MenuItemData selectedItem)
    {
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
        var groupItems = this.AllItems.Where(item => string.Equals(item.RadioGroupId, selectedItem.RadioGroupId, StringComparison.Ordinal));
        foreach (var item in groupItems)
        {
            item.IsChecked = false;
        }

        selectedItem.IsChecked = true;
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
    /// Builds an ObservableCollection of MenuItems for the ultra-fast menu system.
    /// </summary>
    /// <returns>An <see cref="ObservableCollection{MenuItem}"/> containing the configured menu items for the menu system.</returns>
    public ObservableCollection<MenuItemData> BuildMenuSystem()
    {
        var collection = new ObservableCollection<MenuItemData>();
        foreach (var menuItem in this.menuItems)
        {
            collection.Add(menuItem);
        }

        this.BuildLookup();
        return collection;
    }

    /// <summary>
    /// Gets all menu items flattened into a single enumerable, including sub-items.
    /// </summary>
    /// <param name="menuItem">The menu item to flatten.</param>
    /// <returns>All menu items in the hierarchy.</returns>
    private static IEnumerable<MenuItemData> GetAllMenuItemsFlat(MenuItemData menuItem)
    {
        yield return menuItem;
        foreach (var subItem in menuItem.SubItems)
        {
            foreach (var nestedItem in GetAllMenuItemsFlat(subItem))
            {
                yield return nestedItem;
            }
        }
    }

    /// <summary>
    /// Adds a menu flyout item to the specified collection of items.
    /// </summary>
    /// <param name="items">The collection of items to which the menu flyout item will be added.</param>
    /// <param name="menuItem">The menu item to add.</param>
    /// <param name="parentCommand">The command associated with the parent menu item.</param>
    /// <param name="parentFullId">The full identifier of the parent menu item.</param>
    private static void AddMenuFlyoutItem(IList<MenuFlyoutItemBase> items, MenuItemData menuItem, ICommand? parentCommand, string parentFullId = "")
    {
        if (menuItem.IsSeparator)
        {
            items.Add(new MenuFlyoutSeparator());
            return;
        }

        if (menuItem.SubItems.Any())
        {
            var subMenuItem = CreateSubMenuFlyoutItem(menuItem, parentCommand, parentFullId);
            items.Add(subMenuItem);
        }
        else
        {
            var leafMenuItem = CreateLeafMenuFlyoutItem(menuItem, parentCommand, parentFullId);
            items.Add(leafMenuItem);
        }
    }

    /// <summary>
    /// Creates a submenu flyout item with its child items.
    /// </summary>
    /// <param name="menuItem">The menu item to create a submenu for.</param>
    /// <param name="parentCommand">The command associated with the parent menu item.</param>
    /// <param name="parentFullId">The full identifier of the parent menu item.</param>
    /// <returns>A configured MenuFlyoutSubItem.</returns>
    private static MenuFlyoutSubItem CreateSubMenuFlyoutItem(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        var subMenuFlyoutItem = new MenuFlyoutSubItem
        {
            Text = menuItem.Text,
            IsEnabled = menuItem.IsEnabled,
        };

        ConfigureMenuItemIcon(subMenuFlyoutItem, menuItem);

        subMenuFlyoutItem.Items.Clear();
        foreach (var subItem in menuItem.SubItems)
        {
            AddMenuFlyoutItem(subMenuFlyoutItem.Items, subItem, menuItem.Command ?? parentCommand, GetFullId(parentFullId, menuItem.Id));
        }

        return subMenuFlyoutItem;
    }

    /// <summary>
    /// Creates a leaf menu flyout item.
    /// </summary>
    /// <param name="menuItem">The menu item to create a flyout item for.</param>
    /// <param name="parentCommand">The command associated with the parent menu item.</param>
    /// <param name="parentFullId">The full identifier of the parent menu item.</param>
    /// <returns>A configured MenuFlyoutItem or ToggleMenuFlyoutItem based on checkable state.</returns>
    private static MenuFlyoutItemBase CreateLeafMenuFlyoutItem(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        var flyoutItem = menuItem.HasSelectionState
            ? CreateSelectableMenuFlyoutItem(menuItem, parentCommand, parentFullId)
            : CreateCommandMenuFlyoutItem(menuItem, parentCommand, parentFullId);

        ConfigureMenuItemIcon(flyoutItem, menuItem);
        return flyoutItem;
    }

    /// <summary>
    /// Creates a selectable menu flyout item (checkable or radio group).
    /// </summary>
    private static MenuFlyoutItemBase CreateSelectableMenuFlyoutItem(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        return menuItem.Icon != null
            ? CreateSelectableItemWithIcon(menuItem, parentCommand, parentFullId)
            : CreateToggleMenuItem(menuItem, parentCommand, parentFullId);
    }

    /// <summary>
    /// Creates a selectable menu item with semantic icon (uses right-side selection indicator).
    /// </summary>
    private static MenuFlyoutItem CreateSelectableItemWithIcon(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        var flyoutItem = new MenuFlyoutItem
        {
            Text = menuItem.Text,
            Command = menuItem.Command ?? parentCommand,
            CommandParameter = menuItem.Command == null && parentCommand != null ? GetFullId(parentFullId, menuItem.Id) : null,
            IsEnabled = menuItem.IsEnabled,
        };

        SetupRightSideSelectionIndicator(flyoutItem, menuItem);
        return flyoutItem;
    }

    /// <summary>
    /// Creates a toggle menu item (uses standard left-side checkmark).
    /// </summary>
    private static ToggleMenuFlyoutItem CreateToggleMenuItem(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        var toggleItem = new ToggleMenuFlyoutItem
        {
            Text = menuItem.Text,
            Command = menuItem.Command ?? parentCommand,
            CommandParameter = menuItem.Command == null && parentCommand != null ? GetFullId(parentFullId, menuItem.Id) : null,
            IsEnabled = menuItem.IsEnabled,
            KeyboardAcceleratorTextOverride = menuItem.AcceleratorText,
        };

        // Bind flyout toggle IsChecked to data model IsChecked property for checkable behavior
        var checkedBinding = new Binding
        {
            Path = new PropertyPath("IsChecked"),
            Source = menuItem,
            Mode = BindingMode.TwoWay,
        };
        toggleItem.SetBinding(ToggleMenuFlyoutItem.IsCheckedProperty, checkedBinding);

        // Store radio group info for potential future use
        if (!string.IsNullOrEmpty(menuItem.RadioGroupId))
        {
            toggleItem.Tag = $"RadioGroup:{menuItem.RadioGroupId}";
        }

        return toggleItem;
    }

    /// <summary>
    /// Creates a regular command menu flyout item.
    /// </summary>
    private static MenuFlyoutItem CreateCommandMenuFlyoutItem(MenuItemData menuItem, ICommand? parentCommand, string parentFullId)
    {
        return new MenuFlyoutItem
        {
            Text = menuItem.Text,
            Command = menuItem.Command ?? parentCommand,
            CommandParameter = menuItem.Command == null && parentCommand != null ? GetFullId(parentFullId, menuItem.Id) : null,
            IsEnabled = menuItem.IsEnabled,
            KeyboardAcceleratorTextOverride = menuItem.AcceleratorText,
        };
    }

    /// <summary>
    /// Configures the semantic icon for a menu flyout item.
    /// Follows UX principle: semantic icons on left (16x16), framework-rendered checkmarks on right.
    /// Selection state is handled separately by using ToggleMenuFlyoutItem when needed.
    /// </summary>
    /// <param name="menuFlyoutItem">The menu flyout item to configure.</param>
    /// <param name="menuItem">The source menu item.</param>
    private static void ConfigureMenuItemIcon(MenuFlyoutItemBase menuFlyoutItem, MenuItemData menuItem)
    {
        // Always set semantic icon if provided (left side, 16x16)
        // The framework will handle checkmarks separately on the right side
        if (menuItem.Icon != null)
        {
            // Convert IconSource to IconElement
            var iconElement = CreateIconElementFromSource(menuItem.Icon);
            if (iconElement != null)
            {
                switch (menuFlyoutItem)
                {
                    case ToggleMenuFlyoutItem toggleItem:
                        toggleItem.Icon = iconElement;
                        break;
                    case MenuFlyoutItem flyoutItem:
                        flyoutItem.Icon = iconElement;
                        break;
                    case MenuFlyoutSubItem subItem:
                        subItem.Icon = iconElement;
                        break;
                }
            }
        }
    }

    /// <summary>
    /// Gets all menu items, including sub-items, with their full identifiers.
    /// </summary>
    /// <param name="menuItem">The menu item to process.</param>
    /// <param name="currentId">The current identifier for this menu item.</param>
    /// <returns>An enumerable collection of key-value pairs, where the key is the full identifier and the value is the menu item.</returns>
    private static IEnumerable<KeyValuePair<string, MenuItemData>> GetAllMenuItems(MenuItemData menuItem, string currentId)
    {
        yield return new KeyValuePair<string, MenuItemData>(currentId, menuItem);
        foreach (var subItem in menuItem.SubItems)
        {
            foreach (var subPair in GetAllMenuItems(subItem, subItem.Id))
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
    /// Sets up right-side selection indicator for menu items with semantic icons.
    /// Uses KeyboardAcceleratorTextOverride to display selection state on the right side.
    /// </summary>
    /// <param name="menuFlyoutItem">The menu flyout item to configure.</param>
    /// <param name="menuItem">The source menu item containing selection state.</param>
    private static void SetupRightSideSelectionIndicator(MenuFlyoutItem menuFlyoutItem, MenuItemData menuItem)
    {
        // Create a multi-binding to combine accelerator text and selection indicator
        var selectionBinding = new Binding
        {
            Path = new PropertyPath("IsChecked"),
            Source = menuItem,
            Mode = BindingMode.OneWay,
            Converter = new SelectionToRightSideTextConverter { AcceleratorText = menuItem.AcceleratorText, RadioGroupId = menuItem.RadioGroupId },
        };

        menuFlyoutItem.SetBinding(MenuFlyoutItem.KeyboardAcceleratorTextOverrideProperty, selectionBinding);
    }

    /// <summary>
    /// Creates an IconElement from an IconSource.
    /// </summary>
    /// <param name="iconSource">The IconSource to convert.</param>
    /// <returns>An IconElement, or null if conversion fails.</returns>
    private static IconElement? CreateIconElementFromSource(IconSource iconSource)
    {
        return iconSource switch
        {
            SymbolIconSource symbolIconSource => new SymbolIcon { Symbol = symbolIconSource.Symbol },
            FontIconSource fontIconSource => new FontIcon
            {
                Glyph = fontIconSource.Glyph,
                FontFamily = fontIconSource.FontFamily,
                FontSize = fontIconSource.FontSize,
                FontWeight = fontIconSource.FontWeight,
                FontStyle = fontIconSource.FontStyle,
            },
            BitmapIconSource bitmapIconSource => new BitmapIcon { UriSource = bitmapIconSource.UriSource },
            PathIconSource pathIconSource => new PathIcon { Data = pathIconSource.Data },
            _ => null,
        };
    }

    /// <summary>
    /// Normalizes menu item text to create a valid identifier.
    /// </summary>
    /// <param name="text">The menu item text.</param>
    /// <returns>A normalized identifier string.</returns>
    private static string NormalizeTextForId(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return "ITEM";
        }

        // Normalize text for ID generation
        var normalized = text.Replace(" ", "_", StringComparison.Ordinal)
            .Replace(".", "_", StringComparison.Ordinal)
            .Replace("-", "_", StringComparison.Ordinal);

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

    /// <summary>
    /// Ensures the ID is unique among existing IDs by appending a number if needed.
    /// </summary>
    /// <param name="baseId">The base identifier.</param>
    /// <param name="existingIds">Collection of existing identifiers.</param>
    /// <returns>A unique identifier.</returns>
    private static string EnsureUniqueId(string baseId, IEnumerable<string> existingIds)
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
            uniqueId = $"{baseId}_{counter}";
            counter++;
        }
        while (existingSet.Contains(uniqueId));

        return uniqueId;
    }

    /// <summary>
    /// Assigns hierarchical IDs to a menu item and all its sub-items recursively.
    /// </summary>
    /// <param name="menuItem">The menu item to assign IDs to.</param>
    /// <param name="parentId">The parent identifier.</param>
    private static void AssignHierarchicalIds(MenuItemData menuItem, string parentId)
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
            var subItemBaseId = NormalizeTextForId(subItem.Text);
            var subItemId = EnsureUniqueId(subItemBaseId, subItemIds);
            var fullSubItemId = $"{parentId}.{subItemId}";

            subItemIds.Add(subItemId);
            AssignHierarchicalIds(subItem, fullSubItemId);
        }
    }

    /// <summary>
    /// Builds the lookup dictionary for quick access to menu items by their identifiers.
    /// </summary>
    private void BuildLookup() => this.menuItemsLookup = this.menuItems
            .SelectMany(menuItem => GetAllMenuItems(menuItem, menuItem.Id))
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase);
}
