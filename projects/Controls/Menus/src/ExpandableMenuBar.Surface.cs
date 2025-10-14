// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <content>
///     Interaction surface coordination for <see cref="ExpandableMenuBar"/>.
/// </content>
public sealed partial class ExpandableMenuBar
{
    private void ApplyCurrentExpansionState(bool initial)
    {
        Debug.Assert(this.innerMenuBar is not null, "expecting a valid innerMenuBar");

        if (this.IsExpanded)
        {
            this.ApplyExpansion(this.expansionSource, initial);
        }
        else
        {
            this.ApplyCollapse(this.collapseKind, initial);
        }
    }

    private void ApplyExpansion(MenuInteractionInputSource source, bool initial = false)
    {
        Debug.Assert(this.innerMenuBar is not null, "expecting a valid innerMenuBar");

        if (this.MenuSource is not { Items: { Count: > 0 } })
        {
            if (!initial)
            {
                this.RequestCollapse(MenuDismissKind.Programmatic);
            }

            return;
        }

        this.LogExpandingState(source);

        this.UpdateVisualState(useTransitions: !initial);

        var navMode = source.ToNavigationMode();
        this.ActivateMenuBarAfterExpansion(source, navMode);
    }

    private void ApplyCollapse(MenuDismissKind kind, bool initial = false)
    {
        Debug.Assert(this.innerMenuBar is not null, "expecting a valid innerMenuBar");

        this.LogCollapsingState(kind);
        this.UpdateVisualState(useTransitions: !initial);

        if (!initial)
        {
            if (this.innerMenuBar.GetExpandedItem() is not null)
            {
                if (this.MenuSource is { Services.InteractionController: { } controller })
                {
                    var context = MenuInteractionContext.ForRoot(this.innerMenuBar);
                    controller.OnDismissRequested(context, kind);
                }
                else
                {
                    this.innerMenuBar.Dismiss(kind);
                }
            }

            if (this.hamburgerButton is { })
            {
                _ = this.hamburgerButton.Focus(FocusState.Programmatic);
            }
        }
    }

    private void UpdateVisualState(bool useTransitions)
    {
        if (!this.templateApplied)
        {
            return;
        }

        _ = VisualStateManager.GoToState(this, this.IsExpanded ? "Expanded" : "Collapsed", useTransitions);
    }

    private void ReinitializeRootObservers()
    {
        this.DetachRootObservers();

        if (this.MenuSource is not { Items: { } items })
        {
            return;
        }

        if (items is INotifyCollectionChanged collectionChanged)
        {
            collectionChanged.CollectionChanged += this.OnRootItemsCollectionChanged;
            this.rootItemsCollection = collectionChanged;
        }

        foreach (var item in items)
        {
            this.AttachRootItem(item);
        }
    }

    private void DetachRootObservers()
    {
        if (this.rootItemsCollection is not null)
        {
            this.rootItemsCollection.CollectionChanged -= this.OnRootItemsCollectionChanged;
            this.rootItemsCollection = null;
        }

        if (this.rootItemHandlers.Count == 0)
        {
            return;
        }

        foreach (var (item, handler) in this.rootItemHandlers)
        {
            item.PropertyChanged -= handler;
        }

        this.rootItemHandlers.Clear();
    }

    private void OnHamburgerButtonClick(object sender, RoutedEventArgs e)
    {
        var inputSource = e.OriginalSource is UIElement element && element.FocusState == FocusState.Keyboard
            ? MenuInteractionInputSource.KeyboardInput
            : MenuInteractionInputSource.PointerInput;

        if (this.IsExpanded)
        {
            var dismissKind = inputSource == MenuInteractionInputSource.KeyboardInput
                ? MenuDismissKind.KeyboardInput
                : MenuDismissKind.PointerInput;
            this.RequestCollapse(dismissKind);
        }
        else
        {
            this.RequestExpand(inputSource);
        }
    }

    private void OnRootItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e is null)
        {
            return;
        }

        this.LogRootItemsStateChange(e.Action);

        if (e.OldItems is { Count: > 0 })
        {
            foreach (var item in e.OldItems.OfType<MenuItemData>())
            {
                this.DetachRootItem(item);
            }
        }

        if (e.NewItems is { Count: > 0 })
        {
            foreach (var item in e.NewItems.OfType<MenuItemData>())
            {
                this.AttachRootItem(item);
            }
        }

        if (this.MenuSource is not { Items: { } items })
        {
            return;
        }

        if (!items.Any(static item => item.IsExpanded) && this.IsExpanded)
        {
            this.RequestCollapse(MenuDismissKind.Programmatic);
        }
    }

    private void OnRootItemIsExpandedChanged()
    {
        if (this.suppressExpandedCallback)
        {
            return;
        }

        if (!this.IsExpanded)
        {
            return;
        }

        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            if (!this.IsExpanded)
            {
                return;
            }

            if (this.MenuSource is not { Items: { } itemsDeferred })
            {
                return;
            }

            if (itemsDeferred.Any(static item => item.IsExpanded))
            {
                return;
            }

            if (this.innerMenuBar?.GetExpandedItem() is null)
            {
                this.RequestCollapse(MenuDismissKind.Programmatic);
            }
        });
    }

    private void ActivateMenuBarAfterExpansion(MenuInteractionInputSource source, MenuNavigationMode navMode)
    {
        Debug.Assert(this.innerMenuBar is not null, "ActivateMenuBarAfterExpansion requires a valid innerMenuBar.");
        Debug.Assert(this.IsExpanded, "ActivateMenuBarAfterExpansion requires IsExpanded to be true.");

        if (this.MenuSource is not { Services: { InteractionController: { } controller }, Items: { Count: > 0 } items })
        {
            return;
        }

        this.suppressExpandedCallback = true;

        // Ensure the menubar items are realized before we try to focus anything.
        this.innerMenuBar.UpdateLayout();

        var context = MenuInteractionContext.ForRoot(this.innerMenuBar);
        controller.OnMenuRequested(context, source);

        var firstInteractive = items.FirstOrDefault(static i => i.IsInteractive);
        if (firstInteractive is { HasChildren: true })
        {
            controller.OnExpandRequested(context, firstInteractive, source);
        }
    }
}
