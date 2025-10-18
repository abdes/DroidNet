// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <content>
///     Dependency properties for <see cref="ExpandableMenuBar"/>.
/// </content>
public sealed partial class ExpandableMenuBar
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(ExpandableMenuBar),
        new PropertyMetadata(default(IMenuSource), OnMenuSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="DismissOnFlyoutDismissal"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DismissOnFlyoutDismissalProperty = DependencyProperty.Register(
        nameof(DismissOnFlyoutDismissal),
        typeof(bool),
        typeof(ExpandableMenuBar),
        new PropertyMetadata(defaultValue: true, OnDismissOnFlyoutDismissalChanged));

    /// <summary>
    ///     Identifies the <see cref="IsExpanded"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsExpandedProperty = DependencyProperty.Register(
        nameof(IsExpanded),
        typeof(bool),
        typeof(ExpandableMenuBar),
        new PropertyMetadata(defaultValue: false, OnIsExpandedChanged));

    /// <summary>
    ///     Gets or sets the shared menu source rendered by the embedded <see cref="MenuBar"/>.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the embedded menu bar dismisses when its cascaded host closes.
    /// </summary>
    public bool DismissOnFlyoutDismissal
    {
        get => (bool)this.GetValue(DismissOnFlyoutDismissalProperty);
        set => this.SetValue(DismissOnFlyoutDismissalProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the control is currently expanded into a full menu bar.
    /// </summary>
    public bool IsExpanded
    {
        get => (bool)this.GetValue(IsExpandedProperty);
        set => this.SetValue(IsExpandedProperty, value);
    }

    private static void OnMenuSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ExpandableMenuBar control)
        {
            return;
        }

        control.HandleMenuSourceChanged(e.OldValue as IMenuSource, e.NewValue as IMenuSource);
    }

    private static void OnDismissOnFlyoutDismissalChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ExpandableMenuBar control)
        {
            return;
        }

        control.HandleDismissOnFlyoutDismissalChanged((bool)e.NewValue);
    }

    private static void OnIsExpandedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ExpandableMenuBar control)
        {
            return;
        }

        control.HandleIsExpandedChanged((bool)e.OldValue, (bool)e.NewValue);
    }

    private void HandleMenuSourceChanged(IMenuSource? oldSource, IMenuSource? newSource)
    {
        if (ReferenceEquals(oldSource, newSource))
        {
            return;
        }

        this.DetachRootObservers();
        this.ReinitializeRootObservers();

        if (!this.templateApplied)
        {
            return;
        }

        if (this.innerMenuBar is null)
        {
            return;
        }

        this.innerMenuBar.MenuSource = newSource;
        this.ApplyInitialState();
    }

    private void HandleIsExpandedChanged(bool oldValue, bool newValue)
    {
        if (oldValue == newValue)
        {
            return;
        }

        if (newValue)
        {
            if (this.hasPendingExpansionSource)
            {
                this.hasPendingExpansionSource = false;
            }
            else
            {
                this.expansionSource = MenuInteractionInputSource.Programmatic;
            }

            this.ApplyExpansion(this.expansionSource);
            if (!this.suppressLifecycleEvents && this.IsExpanded)
            {
                this.RaiseExpanded();
            }
        }
        else
        {
            if (this.hasPendingCollapseKind)
            {
                this.hasPendingCollapseKind = false;
            }
            else
            {
                this.collapseKind = MenuDismissKind.Programmatic;
            }

            this.ApplyCollapse(this.collapseKind);
            if (!this.suppressLifecycleEvents && !this.IsExpanded)
            {
                this.RaiseCollapsed(this.collapseKind);
            }
        }
    }

    private void HandleDismissOnFlyoutDismissalChanged(bool newValue)
    {
        if (this.innerMenuBar is null)
        {
            return;
        }

        this.innerMenuBar.DismissOnFlyoutDismissal = newValue;
    }
}
