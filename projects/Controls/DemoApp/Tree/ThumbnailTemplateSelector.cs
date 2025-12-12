// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// A <see cref="DataTemplateSelector" /> that can map a <see cref="TreeItemAdapter" /> to a template that can be used to display a
/// <see cref="Thumbnail" /> for it.
/// </summary>
internal sealed partial class ThumbnailTemplateSelector : DataTemplateSelector
{
    /// <inheritdoc/>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
    {
        var key = item switch
        {
            SceneAdapter => "SceneThumbnailTemplate",
            EntityAdapter => "EntityThumbnailTemplate",
            _ => "DefaultThumbnailTemplate",
        };

        // Try to resolve the resource from the container's resource chain first; fall back to Application resources.
        var found = TryFindResourceTemplate(container, key);
        if (found is not null)
        {
            return found;
        }

        // Try to return a default if present
        var defaultTemplate = TryFindResourceTemplate(container, "DefaultThumbnailTemplate");
        return defaultTemplate ?? base.SelectTemplateCore(item, container);
    }

    private static DataTemplate? TryFindResourceTemplate(DependencyObject? container, string key)
    {
        if (container is global::Microsoft.UI.Xaml.FrameworkElement fe)
        {
            // Walk up the parent chain and check resources
            var current = fe as global::Microsoft.UI.Xaml.FrameworkElement;
            while (current is not null)
            {
                if (current.Resources?.ContainsKey(key) == true)
                {
                    return current.Resources[key] as DataTemplate;
                }

                // Move up the visual/logical tree
                current = current.Parent as global::Microsoft.UI.Xaml.FrameworkElement;
            }
        }

        return Application.Current?.Resources?.ContainsKey(key) == true
            ? Application.Current.Resources[key] as DataTemplate
            : null;
    }
}
