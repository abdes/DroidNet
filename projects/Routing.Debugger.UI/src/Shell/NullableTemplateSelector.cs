// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class NullableTemplateSelector : DataTemplateSelector
{
    public DataTemplate? NotNullTemplate { get; set; }

    public DataTemplate? NullTemplate { get; set; }

    protected override DataTemplate SelectTemplateCore(object? item, DependencyObject container)
    {
        Debug.Assert(
            this.NullTemplate is not null && this.NotNullTemplate is not null,
            "set the properties of the converter before using it");
        return item == null ? this.NullTemplate! : this.NotNullTemplate!;
    }
}
