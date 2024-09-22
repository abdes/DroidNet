// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class ItemThumbnailTemplateSelector : DataTemplateSelector
{
    public DataTemplate? DefaultTemplate { get; set; }

    public DataTemplate? SceneTemplate { get; set; }

    public DataTemplate? EntityTemplate { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
    {
        if (item is SceneAdapter)
        {
            return this.SceneTemplate;
        }

        if (item is EntityAdapter entity && entity.AttachedObject.Name.EndsWith('1'))
        {
            return this.EntityTemplate;
        }

        return this.DefaultTemplate;
    }
}
