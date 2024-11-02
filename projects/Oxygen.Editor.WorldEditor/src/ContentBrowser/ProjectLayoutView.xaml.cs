// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// The View for the Project Layout pane in the <see cref="ContentBrowserView" />.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
public sealed partial class ProjectLayoutView
{
    public ProjectLayoutView()
    {
        this.InitializeComponent();
        Debug.WriteLine("ProjectLayoutView_Constructor");
    }

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        Debug.WriteLine("ProjectLayoutView_OnLoaded");

        await this.ViewModel!.LoadProjectCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }
}

/// <summary>
/// A special converter that will provide the glyph for a folder icon, based on whether the item is expanded in the tree control
/// or not.
/// </summary>
internal sealed partial class ExpandedToIconGlyphConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var item = (FolderTreeItemAdapter)value;
        return item is { IsExpanded: true, ChildrenCount: > 0 } ? "\uE838" : "\uE8B7";
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
