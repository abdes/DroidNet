// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presents a compact hamburger entry point that space-swaps into a full <see cref="MenuBar"/> when expanded.
/// </summary>
/// <remarks>
///     The control embeds a standard <see cref="MenuBar"/> and coordinates expansion through the shared menu
///     interaction controller so consumers can author their menu structure once and reuse it across surfaces.
/// </remarks>
public sealed partial class ExpandableMenuBar : Control
{
    private const string RootGridPart = "PART_RootGrid";
    private const string HamburgerButtonPart = "PART_HamburgerButton";
    private const string MenuBarContainerPart = "PART_MenuBarContainer";
    private const string MenuBarPart = "PART_MenuBar";

    private readonly Dictionary<MenuItemData, PropertyChangedEventHandler> rootItemHandlers = new();

    private FrameworkElement? rootGrid;
    private ButtonBase? hamburgerButton;
    private FrameworkElement? menuBarContainer;
    private MenuBar? innerMenuBar;
    private INotifyCollectionChanged? rootItemsCollection;
    private bool templateApplied;
    private bool suppressExpandedCallback;
    private bool suppressLifecycleEvents;
    private bool hasPendingExpansionSource;
    private bool hasPendingCollapseKind;
    private Func<ICascadedMenuHost>? hostFactory;
    private MenuInteractionInputSource expansionSource = MenuInteractionInputSource.Programmatic;
    private MenuDismissKind collapseKind = MenuDismissKind.Programmatic;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ExpandableMenuBar"/> class.
    /// </summary>
    public ExpandableMenuBar()
    {
        this.DefaultStyleKey = typeof(ExpandableMenuBar);
        this.Unloaded += this.OnExpandableMenuBarUnloaded;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        this.DetachTemplateParts();

        base.OnApplyTemplate();

        this.rootGrid = this.GetTemplateChild(RootGridPart) as FrameworkElement;
        this.hamburgerButton = this.GetTemplateChild(HamburgerButtonPart) as ButtonBase
            ?? throw new InvalidOperationException($"{nameof(ExpandableMenuBar)} template must declare a ButtonBase named '{HamburgerButtonPart}'.");
        this.menuBarContainer = this.GetTemplateChild(MenuBarContainerPart) as FrameworkElement
            ?? throw new InvalidOperationException($"{nameof(ExpandableMenuBar)} template must declare a FrameworkElement named '{MenuBarContainerPart}'.");
        this.innerMenuBar = this.GetTemplateChild(MenuBarPart) as MenuBar
            ?? throw new InvalidOperationException($"{nameof(ExpandableMenuBar)} template must declare a {nameof(MenuBar)} named '{MenuBarPart}'.");

        this.hamburgerButton.Click += this.OnHamburgerButtonClick;
        this.innerMenuBar.Dismissed += this.OnInnerMenuBarDismissed;

        this.SyncHostFactory();

        if (this.MenuSource is not null)
        {
            this.innerMenuBar.MenuSource = this.MenuSource;
        }

        this.templateApplied = true;
        this.UpdateVisualState(useTransitions: false);
        this.ReinitializeRootObservers();
        this.ApplyInitialState();
    }

    private void DetachTemplateParts()
    {
        if (this.hamburgerButton is not null)
        {
            this.hamburgerButton.Click -= this.OnHamburgerButtonClick;
        }

        if (this.innerMenuBar is not null)
        {
            this.innerMenuBar.Dismissed -= this.OnInnerMenuBarDismissed;
        }

        this.templateApplied = false;
        this.rootGrid = null;
        this.hamburgerButton = null;
        this.menuBarContainer = null;
        this.innerMenuBar = null;
    }

    private void OnExpandableMenuBarUnloaded(object? sender, RoutedEventArgs e)
    {
        this.DetachRootObservers();
    }

    private void AttachRootItem(MenuItemData item)
    {
        if (this.rootItemHandlers.ContainsKey(item))
        {
            return;
        }

        void Handler(object? sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName is nameof(MenuItemData.IsExpanded))
            {
                this.OnRootItemIsExpandedChanged();
            }
        }

        item.PropertyChanged += Handler;
        this.rootItemHandlers[item] = Handler;
    }

    private void DetachRootItem(MenuItemData item)
    {
        if (!this.rootItemHandlers.TryGetValue(item, out var handler))
        {
            return;
        }

        item.PropertyChanged -= handler;
        this.rootItemHandlers.Remove(item);
    }

    private void ApplyInitialState()
    {
        Debug.Assert(this.innerMenuBar is not null, "ApplyInitialState requires a valid innerMenuBar");

        var previous = this.suppressLifecycleEvents;
        this.suppressLifecycleEvents = true;
        try
        {
            this.ApplyCurrentExpansionState(initial: true);
        }
        finally
        {
            this.suppressLifecycleEvents = previous;
        }
    }

    private void RequestExpand(MenuInteractionInputSource source)
    {
        this.expansionSource = source;

        if (this.IsExpanded)
        {
            return;
        }

        this.hasPendingExpansionSource = true;
        this.SetValue(IsExpandedProperty, true);
    }

    private void RequestCollapse(MenuDismissKind kind)
    {
        this.collapseKind = kind;

        if (!this.IsExpanded)
        {
            return;
        }

        this.hasPendingCollapseKind = true;
        this.SetValue(IsExpandedProperty, false);
    }

    private void OnInnerMenuBarDismissed(object? sender, MenuDismissedEventArgs e)
    {
        if (!this.IsExpanded)
        {
            return;
        }

        this.RequestCollapse(e.Kind);
    }
}
