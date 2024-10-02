// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class ThumbnailTemplateSelector : DataTemplateSelector
{
    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container)
    {
        if (item is SceneAdapter)
        {
            return (DataTemplate)Application.Current.Resources["SceneThumbnailTemplate"];
        }

        if (item is EntityAdapter)
        {
            return (DataTemplate)Application.Current.Resources["EntityThumbnailTemplate"];
        }

        return (DataTemplate)Application.Current.Resources["DefaultThumbnailTemplate"];
    }
}
