// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;

namespace Oxygen.Editor.World.Problems;

/// <summary>
/// The Problems panel: problems grouped by file beside the full diagnostic of the selected one.
/// </summary>
[ViewModel(typeof(ProblemsViewModel))]
public sealed partial class ProblemsView : UserControl
{
    private ProblemsViewModel? observed;

    /// <summary>Initializes a new instance of the <see cref="ProblemsView"/> class.</summary>
    public ProblemsView()
    {
        this.InitializeComponent();
        this.ViewModelChanged += (_, _) => this.Attach();
        this.Unloaded += (_, _) => this.Detach();
        this.Loaded += (_, _) => this.Attach();
    }

    /// <summary>Gets the brush of a severity glyph.</summary>
    /// <param name="isError">Whether the problem is an error.</param>
    /// <param name="isWarning">Whether the problem is a warning.</param>
    /// <returns>The critical, caution or secondary text brush.</returns>
    public static Brush? SeverityBrush(bool isError, bool isWarning)
        => Application.Current.Resources.TryGetValue(
            isError ? "SystemFillColorCriticalBrush" : isWarning ? "SystemFillColorCautionBrush" : "TextFillColorSecondaryBrush",
            out var brush) ? brush as Brush : null;

    /// <summary>Hides an element while a flag is set.</summary>
    /// <param name="value">The flag.</param>
    /// <returns>Collapsed when the flag is set; otherwise visible.</returns>
    public static Visibility CollapsedWhen(bool value) => value ? Visibility.Collapsed : Visibility.Visible;

    private void Attach()
    {
        if (ReferenceEquals(this.observed, this.ViewModel))
        {
            return;
        }

        this.Detach();
        this.observed = this.ViewModel;
        if (this.observed is null)
        {
            return;
        }

        this.ProblemsSource.Source = this.observed.Groups;
        this.ProblemList.ItemsSource = this.ProblemsSource.View;
        this.observed.PropertyChanged += this.OnViewModelPropertyChanged;
        this.ProblemList.SelectedItem = this.observed.Selected;
        this.UpdateDetailColumn();
    }

    private void Detach()
    {
        if (this.observed is not null)
        {
            this.observed.PropertyChanged -= this.OnViewModelPropertyChanged;
            this.observed = null;
        }
    }

    // The details take part of the panel only while a problem is selected.
    private void UpdateDetailColumn()
    {
        var open = this.observed?.HasSelection == true;
        this.DetailColumn.Width = open ? new GridLength(2, GridUnitType.Star) : new GridLength(0);
        this.DetailColumn.MinWidth = open ? 240 : 0;
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(ProblemsViewModel.HasSelection), StringComparison.Ordinal))
        {
            this.UpdateDetailColumn();
        }

        if (string.Equals(args.PropertyName, nameof(ProblemsViewModel.Selected), StringComparison.Ordinal)
            && !ReferenceEquals(this.ProblemList.SelectedItem, this.observed?.Selected))
        {
            this.ProblemList.SelectedItem = this.observed?.Selected;
        }
    }

    private void OnSelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (this.observed is not null && this.ProblemList.SelectedItem is ProblemEntry entry)
        {
            this.observed.Selected = entry;
        }
    }

    private async void OnProblemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (this.observed is not null && sender is FrameworkElement { DataContext: ProblemEntry entry })
        {
            args.Handled = true;
            await this.observed.OpenAsync(entry).ConfigureAwait(true);
        }
    }

    private async void OnProblemListKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Key == VirtualKey.Enter && this.observed?.Selected is { } entry)
        {
            args.Handled = true;
            await this.observed.OpenAsync(entry).ConfigureAwait(true);
        }
    }
}
