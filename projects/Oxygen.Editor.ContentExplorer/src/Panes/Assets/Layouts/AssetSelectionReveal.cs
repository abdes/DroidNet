// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>Brings an explicitly requested row into view without moving focus after a newer user action.</summary>
internal static class AssetSelectionReveal
{
    /// <summary>Reveals one current navigation request after the selector has loaded.</summary>
    /// <param name="selector">The visible list or tile selector.</param>
    /// <param name="model">The active layout owner.</param>
    internal static void Apply(ListViewBase selector, AssetsLayoutViewModel model)
    {
        if (!selector.IsLoaded || model.TakeSelectionToReveal() is not { } row)
        {
            return;
        }

        var previousFocus = FocusManager.GetFocusedElement(selector.XamlRoot);
        selector.ScrollIntoView(row);
        _ = selector.DispatcherQueue.TryEnqueue(() =>
        {
            if (!selector.IsLoaded || !ReferenceEquals(model.SelectedRow, row))
            {
                return;
            }

            var focus = FocusManager.GetFocusedElement(selector.XamlRoot);
            if (ReferenceEquals(focus, previousFocus) || IsInside(focus as DependencyObject, selector))
            {
                _ = (selector.ContainerFromItem(row) as Control ?? selector).Focus(FocusState.Programmatic);
            }
        });
    }

    private static bool IsInside(DependencyObject? element, DependencyObject ancestor)
    {
        while (element is not null)
        {
            if (ReferenceEquals(element, ancestor))
            {
                return true;
            }

            element = VisualTreeHelper.GetParent(element);
        }

        return false;
    }
}
