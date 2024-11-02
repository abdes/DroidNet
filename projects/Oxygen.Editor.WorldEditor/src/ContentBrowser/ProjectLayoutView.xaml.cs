// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// The View for the Project Layout pane in the <see cref="ContentBrowserView" />.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
public sealed partial class ProjectLayoutView
{
    public ProjectLayoutView() => this.InitializeComponent();
}

/// <summary>
/// A special converter that will provide the glyph for a folder icon, based on whether the item is expanded in the tree control
/// or not.
/// </summary>
internal sealed partial class ExpandedToIconGlyphConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var isExpanded = (bool)value;
        return isExpanded ? "\uE838" : "\uE8B7";
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
