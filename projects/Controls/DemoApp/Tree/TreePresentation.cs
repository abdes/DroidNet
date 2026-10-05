// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Applies native density styles and demo-specific row spacing.</summary>
internal static class TreePresentation
{
    /// <summary>Switches the tree's scoped density resources and responsive icon and indentation metrics.</summary>
    /// <param name="tree">The tree to update.</param>
    /// <param name="isCompact">Whether to use WinUI's compact density resources.</param>
    /// <param name="isNarrow">Whether to reduce icon spacing and indentation for a narrow viewport.</param>
    public static void ApplyDensity(DynamicTree tree, bool isCompact, bool isNarrow)
    {
        var source = new Uri(isCompact
            ? "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/CompactTreeDensityStyles.xaml"
            : "ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/TreeDensityStyles.xaml");
        var density = tree.Resources.MergedDictionaries.FirstOrDefault(dictionary => dictionary.Source == source);
        if (density is null)
        {
            foreach (var previous in tree.Resources.MergedDictionaries.Where(dictionary =>
                dictionary.Source?.AbsolutePath.EndsWith("TreeDensityStyles.xaml", StringComparison.Ordinal) == true).ToArray())
            {
                _ = tree.Resources.MergedDictionaries.Remove(previous);
            }

            density = new ResourceDictionary { Source = source };
            tree.Resources.MergedDictionaries.Add(density);
        }

        tree.Style = (Style)density["TreeDensityStyle"];
        tree.ItemIconSize = isCompact ? 18 : 24;
        var iconMargin = isNarrow ? (isCompact ? 1 : 2) : (isCompact ? 2 : 4);
        tree.ItemIconMargin = new Thickness(iconMargin, 0, iconMargin, 0);
        tree.ItemIndentWidth = isNarrow
            ? isCompact ? 18 : 22
            : isCompact ? 28 : 34;
    }
}
