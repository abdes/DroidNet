// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Displays scoped cooking progress, recovery, and output in a dockable panel.</summary>
[ViewModel(typeof(CookingPanelViewModel))]
public sealed partial class CookingPanelView : UserControl
{
    private double expandedRunWidth = 220;
    private bool isNarrow;

    /// <summary>Initializes a new instance of the <see cref="CookingPanelView"/> class.</summary>
    public CookingPanelView()
    {
        this.InitializeComponent();
        this.SizeChanged += this.OnSizeChanged;
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs args)
    {
        var narrow = args.NewSize.Width < 560;
        if (this.isNarrow == narrow)
        {
            return;
        }

        this.isNarrow = narrow;
        if (narrow)
        {
            this.expandedRunWidth = this.RunColumn.ActualWidth;
        }

        this.RunColumn.MinWidth = narrow ? 0 : 150;
        this.RunColumn.Width = new GridLength(narrow ? 0 : this.expandedRunWidth);
        this.RunList.Visibility = this.RunSplitter.Visibility = narrow ? Visibility.Collapsed : Visibility.Visible;
        this.NarrowRunPicker.Visibility = narrow ? Visibility.Visible : Visibility.Collapsed;
        Grid.SetColumnSpan(this.RunSidebar, narrow ? 3 : 1);
        Grid.SetRowSpan(this.RunSidebar, narrow ? 1 : 2);
        Grid.SetRow(this.RunDetails, narrow ? 1 : 0);
        Grid.SetRowSpan(this.RunDetails, narrow ? 1 : 2);
        Grid.SetRow(this.EmptyMessage, narrow ? 1 : 0);
        Grid.SetRowSpan(this.EmptyMessage, narrow ? 1 : 2);
    }

    private void OnHeaderSizeChanged(object sender, SizeChangedEventArgs args)
    {
        var actionsWidth = (this.RetryButton.Visibility == Visibility.Visible ? this.RetryButton.DesiredSize.Width : 0)
            + (this.CancelButton.Visibility == Visibility.Visible ? this.CancelButton.DesiredSize.Width : 0);
        this.RunTitle.MaxWidth = Math.Max(48, args.NewSize.Width - this.RunStatus.DesiredSize.Width - actionsWidth - 32);
    }

    private async void GoToPropertyClicked(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: CookingIssueViewModel issue } && this.ViewModel is { } model)
        {
            await model.GoToPropertyCommand.ExecuteAsync(issue).ConfigureAwait(true);
        }
    }

    private async void OpenDocumentClicked(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState document } && this.ViewModel is { } model)
        {
            await model.OpenDocumentCommand.ExecuteAsync(document).ConfigureAwait(true);
        }
    }
}
