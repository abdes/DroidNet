// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspection;

/// <summary>Displays an explicit read-only cooked-output report in the document host.</summary>
[ViewModel(typeof(CookedInspectionViewModel))]
public sealed partial class CookedInspectionView
{
    /// <summary>Initializes a new instance of the <see cref="CookedInspectionView"/> class.</summary>
    public CookedInspectionView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => _ = this.ViewModel?.InitializeAsync();
    }

    private void OnSectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            model.Section = ReferenceEquals(sender.SelectedItem, this.FilesSection) ? 1 : ReferenceEquals(sender.SelectedItem, this.IssuesSection) ? 2 : 0;
        }
    }

    private void OnShowAsset(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { Tag: Uri uri })
        {
            this.ViewModel?.ShowAssetCommand.Execute(uri);
        }
    }
}
