// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     A button control that shows a menu when clicked and acts as a root surface for keyboard navigation.
/// </summary>
[SuppressMessage(
    "Microsoft.Design",
    "CA1001:TypesThatOwnDisposableFieldsShouldBeDisposable",
    Justification = "WinUI controls follow framework pattern of cleanup in Unloaded event and destructor, not IDisposable")]
public sealed partial class MenuButton : Button, IRootMenuSurface
{
    private PopupMenuHost? menuHost;
    private MenuItemData? buttonItemData;
    private bool isMenuOpen;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuButton"/> class.
    /// </summary>
    public MenuButton()
    {
        this.Click += this.OnButtonClick;

        this.GettingFocus += (sender, args) =>
        {
            if (this.MenuSource is not { Services.InteractionController: { } controller })
            {
                return;
            }

            controller.OnGettingFocus(this.CreateRootContext(), args.OldFocusedElement);
        };

        this.GotFocus += this.OnButtonGotFocus;
        this.LostFocus += this.OnButtonLostFocus;
        this.PointerEntered += this.OnButtonPointerEntered;
        this.PointerExited += this.OnButtonPointerExited;
        this.PreviewKeyDown += this.OnButtonPreviewKeyDown;
        this.Unloaded += (sender, e) =>
        {
            if (this.menuHost is not null)
            {
                this.menuHost.Opening -= this.OnMenuOpening;
                this.menuHost.Opened -= this.OnMenuOpened;
                this.menuHost.Closing -= this.OnMenuClosing;
                this.menuHost.Closed -= this.OnMenuClosed;
                this.menuHost.Dispose();
                this.menuHost = null;
            }
        };

        this.ApplyChromeStyle();
    }

    /// <summary>
    ///     Gets a value indicating whether the menu is currently open.
    /// </summary>
    public bool IsMenuOpen
    {
        get => this.isMenuOpen;
        private set
        {
            if (this.isMenuOpen != value)
            {
                this.isMenuOpen = value;
                _ = VisualStateManager.GoToState(this, value ? "MenuOpen" : "MenuClosed", useTransitions: true);
            }
        }
    }

    /// <summary>
    /// Gets the MenuItemData representing this button as a root menu item.
    /// </summary>
    private MenuItemData ButtonItemData
    {
        get
        {
            if (this.buttonItemData is null && this.MenuSource is not null)
            {
                this.buttonItemData = new MenuItemData
                {
                    Id = "MenuButton",
                    Text = this.Content?.ToString() ?? "Menu",
                    SubItems = [.. this.MenuSource.Items],
                };
            }

            return this.buttonItemData ?? new MenuItemData { Id = "MenuButton", Text = "Menu" };
        }
    }

    /// <inheritdoc />
    public bool Show(MenuNavigationMode navigationMode)
    {
        if (this.MenuSource is null)
        {
            return false;
        }

        if (this.IsMenuOpen)
        {
            return true;
        }

        // Create host if needed
        if (this.menuHost is null)
        {
            this.menuHost = new PopupMenuHost
            {
                MenuSource = this.MenuSource,
                RootSurface = this,
                MaxLevelHeight = this.MaxMenuHeight,
            };

            this.menuHost.Opening += this.OnMenuOpening;
            this.menuHost.Opened += this.OnMenuOpened;
            this.menuHost.Closing += this.OnMenuClosing;
            this.menuHost.Closed += this.OnMenuClosed;
        }
        else
        {
            // Update properties in case they changed
            this.menuHost.MenuSource = this.MenuSource;
            this.menuHost.MaxLevelHeight = this.MaxMenuHeight;
        }

        // Show the menu below the button
        this.menuHost.ShowAt(this, navigationMode);

        return true;
    }

    private MenuInteractionContext CreateRootContext(ICascadedMenuSurface? columnSurface = null)
    {
        var surface = columnSurface;
        if (surface is null && this.menuHost is { IsOpen: true } host)
        {
            surface = host.Surface;
        }

        return MenuInteractionContext.ForRoot(this, surface);
    }

    private void OnButtonClick(object sender, RoutedEventArgs e)
    {
        if (this.MenuSource is null)
        {
            return;
        }

        if (this.IsMenuOpen)
        {
            this.menuHost?.Dismiss(MenuDismissKind.Programmatic);
            return;
        }

        // Notify controller that the button requests expansion (showing the menu)
        if (this.MenuSource.Services.InteractionController is { } controller)
        {
            var inputSource = e.OriginalSource is UIElement uie && uie.FocusState == FocusState.Keyboard
                ? MenuInteractionInputSource.KeyboardInput
                : MenuInteractionInputSource.PointerInput;

            controller.OnMenuRequested(this.CreateRootContext(), inputSource);
        }
    }

    private void OnButtonGotFocus(object sender, RoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        var uie = e.OriginalSource as UIElement;
        var fc = uie?.FocusState;

        var inputSource = fc switch
        {
            FocusState.Keyboard => MenuInteractionInputSource.KeyboardInput,
            FocusState.Pointer => MenuInteractionInputSource.PointerInput,
            FocusState.Programmatic => MenuInteractionInputSource.Programmatic,
            _ => MenuInteractionInputSource.Programmatic,
        };

        controller.OnItemGotFocus(this.CreateRootContext(), this.ButtonItemData, inputSource);
    }

    private void OnButtonLostFocus(object? sender, RoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        controller.OnItemLostFocus(this.CreateRootContext(), this.ButtonItemData);
    }

    private void OnButtonPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        controller.OnItemHoverStarted(this.CreateRootContext(), this.ButtonItemData);
    }

    private void OnButtonPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        controller.OnItemHoverEnded(this.CreateRootContext(), this.ButtonItemData);
    }

    private void OnButtonPreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        var handled = false;
        if (e.Key == VirtualKey.Escape)
        {
            MenuInteractionContext context;
            if (this.ButtonItemData.IsExpanded)
            {
                Debug.Assert(this.menuHost is { IsOpen: true }, "Expecting menuHost to be open if button is expanded.");
                context = MenuInteractionContext.ForColumn(MenuLevel.First, this.menuHost, this);
            }
            else
            {
                context = MenuInteractionContext.ForRoot(this);
            }

            handled = controller.OnDismissRequested(context, MenuDismissKind.KeyboardInput);
            if (handled)
            {
                e.Handled = true;
            }

            return;
        }

        MenuNavigationDirection? direction = e.Key switch
        {
            VirtualKey.Left => MenuNavigationDirection.Left,
            VirtualKey.Right => MenuNavigationDirection.Right,
            VirtualKey.Up => MenuNavigationDirection.Up,
            VirtualKey.Down => MenuNavigationDirection.Down,
            _ => null,
        };

        if (direction is not null)
        {
            var context = this.CreateRootContext();
            handled = controller.OnDirectionalNavigation(context, this.ButtonItemData, direction.Value, MenuInteractionInputSource.KeyboardInput);
        }

        if (handled)
        {
            e.Handled = true;
        }
    }

    private void OnMenuOpening(object? sender, EventArgs e)
    {
    }

    private void OnMenuOpened(object? sender, EventArgs e)
    {
        this.IsMenuOpen = true;
        this.ButtonItemData.IsExpanded = true;

        // Setup initial keyboard navigation
        if (sender is ICascadedMenuHost host)
        {
            host.SetupInitialKeyboardNavigation();
        }
    }

    private void OnMenuClosing(object? sender, MenuHostClosingEventArgs e)
    {
    }

    private void OnMenuClosed(object? sender, EventArgs e)
    {
        this.IsMenuOpen = false;
        this.ButtonItemData.IsExpanded = false;

        // Return focus to the button
        _ = this.Focus(FocusState.Programmatic);
    }
}
