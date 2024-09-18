// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.TreeView;

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class DefaultItemThumbnailTemplateSelector : DataTemplateSelector
{
    public DataTemplate? DefaultTemplate { get; set; }

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container)
    {
        Debug.Assert(
            this.DefaultTemplate is not null,
            $"you must set the [{nameof(this.DefaultTemplate)}] property of a [{nameof(DefaultItemThumbnailTemplateSelector)}] template selector");

        return this.DefaultTemplate;
    }
}
