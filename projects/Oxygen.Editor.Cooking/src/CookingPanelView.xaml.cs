// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.Cooking;

/// <summary>Displays scoped cooking progress, recovery, and output in a dockable panel.</summary>
[ViewModel(typeof(CookingPanelViewModel))]
public sealed partial class CookingPanelView : UserControl
{
    private const double WideIssuesWidth = 600;

    private double expandedRunWidth = 220;
    private bool isNarrow;

    /// <summary>Initializes a new instance of the <see cref="CookingPanelView"/> class.</summary>
    public CookingPanelView()
    {
        this.InitializeComponent();
        this.InitializeReadingTracking();
        this.SizeChanged += this.OnSizeChanged;
        this.RunStatus.SizeChanged += (_, _) => this.UpdateHeaderWidth();
        this.RetryButton.SizeChanged += (_, _) => this.UpdateHeaderWidth();
        this.CancelButton.SizeChanged += (_, _) => this.UpdateHeaderWidth();
        this.InspectButton.SizeChanged += (_, _) => this.UpdateHeaderWidth();
        this.ShowImportedAssetsButton.SizeChanged += (_, _) => this.UpdateHeaderWidth();
    }

    private void OnRowContentLoaded(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement content && content.FindAscendant<SelectorItem>() is { } container)
        {
            container.SetBinding(AutomationProperties.NameProperty, new Binding
            {
                Source = content,
                Path = new PropertyPath("DataContext.AccessibleName"),
                Mode = BindingMode.OneWay,
            });
        }
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
        => this.UpdateHeaderWidth();

    private void UpdateHeaderWidth()
    {
        var actionsWidth = (this.RetryButton.Visibility == Visibility.Visible ? this.RetryButton.DesiredSize.Width : 0)
            + (this.CancelButton.Visibility == Visibility.Visible ? this.CancelButton.DesiredSize.Width : 0)
            + (this.InspectButton.Visibility == Visibility.Visible ? this.InspectButton.DesiredSize.Width : 0)
            + (this.ShowImportedAssetsButton.Visibility == Visibility.Visible ? this.ShowImportedAssetsButton.DesiredSize.Width : 0);
        this.RunTitle.MaxWidth = Math.Max(48, this.RunHeader.ActualWidth - this.RunStatus.DesiredSize.Width - actionsWidth - 48);
    }

    private void ShowIssuesClicked(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel?.SelectedRun is { } run)
        {
            run.ViewIndex = CookingRunViewModel.IssuesView;
        }
    }

    // The details sit beside the list when the view is wide, as in a bottom dock, and below it when the view is narrow.
    private void OnIssuesViewSizeChanged(object sender, SizeChangedEventArgs args)
    {
        var wide = args.NewSize.Width >= WideIssuesWidth;
        this.IssueListColumn.Width = new GridLength(wide ? 11 : 1, GridUnitType.Star);
        this.IssueDetailsColumn.Width = wide ? new GridLength(9, GridUnitType.Star) : new GridLength(0);
        this.IssueListRow.Height = wide ? new GridLength(1, GridUnitType.Star) : new GridLength(2, GridUnitType.Star);
        this.IssueDetailsRow.Height = wide ? new GridLength(0) : new GridLength(3, GridUnitType.Star);
        Grid.SetRow(this.IssueDetails, wide ? 0 : 1);
        Grid.SetColumn(this.IssueDetails, wide ? 1 : 0);
        this.IssueDetails.BorderThickness = wide ? new Thickness(1, 0, 0, 0) : new Thickness(0, 1, 0, 0);
    }

    private void CopyPlacesClicked(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel?.SelectedRun?.SelectedIssue is { } issue)
        {
            var package = new Windows.ApplicationModel.DataTransfer.DataPackage();
            package.SetText(issue.CopyText);
            Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
        }
    }

    private async void GoToPropertyClicked(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: CookingIssueViewModel issue } && this.ViewModel is { } model)
        {
            await model.GoToPropertyCommand.ExecuteAsync(issue).ConfigureAwait(true);
        }
    }
}
