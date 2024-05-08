// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using Microsoft.UI.Xaml.Controls;

public class NavigationItem
{
    public NavigationItem(string text, Symbol icon, string accessKey, Type target)
    {
        this.Text = text;
        this.Icon = icon;
        this.AccessKey = accessKey;
        this.TargetViewModel = target;
    }

    public string Text { get; }

    public Symbol Icon { get; }

    public string AccessKey { get; }

    public Type TargetViewModel { get; }
}
