// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// The context menu of something that overrides its source, a row or a component: reset, then the commands an
/// <see cref="IOverrideActionSource"/> supplies when the menu opens, such as <c>Apply to Watchtower</c>.
/// </summary>
public static class OverrideActionMenu
{
    /// <summary>Handles a context request: opens the menu where it was asked for, once the commands are read.</summary>
    /// <param name="target">The element the menu belongs to.</param>
    /// <param name="args">The context request, which this marks handled.</param>
    /// <param name="source">The source of the commands, or <see langword="null"/> when there are none besides reset.</param>
    /// <param name="resetText">The reset item's text, or <see langword="null"/> when the menu has no reset.</param>
    /// <param name="reset">The reset command, or <see langword="null"/> when the menu has no reset.</param>
    /// <returns>A task that completes once the menu opened, or nothing was to show.</returns>
    public static async Task ShowAsync(FrameworkElement target, ContextRequestedEventArgs args, IOverrideActionSource? source, string? resetText = null, ICommand? reset = null)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(args);
        args.Handled = true;
        var position = args.TryGetPosition(target, out var point) ? point : new Point(target.ActualWidth / 2, target.ActualHeight / 2);
        var actions = source is null ? [] : await source.GetActionsAsync().ConfigureAwait(true);
        var menu = new MenuFlyout();
        if (reset is not null)
        {
            menu.Items.Add(new MenuFlyoutItem { Text = resetText ?? "Reset", Command = reset });
        }

        if (menu.Items.Count != 0 && actions.Count != 0)
        {
            menu.Items.Add(new MenuFlyoutSeparator());
        }

        foreach (var action in actions)
        {
            menu.Items.Add(new MenuFlyoutItem { Text = action.Label, Command = action.Command });
        }

        if (menu.Items.Count != 0 && target.XamlRoot is not null)
        {
            menu.ShowAt(target, new FlyoutShowOptions { Position = position });
        }
    }
}
