// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.WinUI;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     A UI control that displays log entries in a live, filterable, and searchable console view.
///     This partial type contains loading, theming, the row presentation and scrolling.
/// </summary>
/// <remarks>
///     The view pulls entries from an <see cref="OutputLogBuffer" /> on the UI thread when the buffer signals new
///     ones, projects them into rows with <see cref="OutputLogProjection" />, and applies only the changes to its
///     row collection: rows are never cleared and re-added.
/// </remarks>
public sealed partial class OutputConsoleView
{
    private const double BottomThreshold = 32.0;

    private readonly OutputLogProjection projection;
    private readonly TypedEventHandler<ListViewBase, ContainerContentChangingEventArgs> contentChangingHandler;
    private readonly Action<OutputLogRow> requestDetails;
    private ObservableCollection<OutputLogRow> viewItems = [];
    private ConsoleColumns columns = new(CharWidth: 7, ShowTimestamps: false, ShowSubsystem: true, SubsystemChars: 0, WordWrap: false);
    private ConsoleBrushes? brushes;
    private ScrollViewer? scrollViewer;
    private bool isLoaded;
    private bool levelsWired;
    private bool awaitingScrollToEnd;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputConsoleView" /> class.
    /// </summary>
    public OutputConsoleView()
    {
        this.InitializeComponent();
        this.projection = new OutputLogProjection(this.CurrentFilter());
        this.contentChangingHandler = this.OnContainerContentChanging;
        this.requestDetails = this.ShowDetails;
        this.pauseTimer = this.CreateTimer(TimeSpan.FromMilliseconds(500), repeating: true, this.UpdatePauseLabel);
        this.searchTimer = this.CreateTimer(TimeSpan.FromMilliseconds(150), repeating: false, this.ApplySearch);

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.ActualThemeChanged += this.OnActualThemeChanged;
        this.WireList();
        this.WireToolbar();
        this.WireDetailsPane();
    }

    private DispatcherQueueTimer CreateTimer(TimeSpan interval, bool repeating, Action tick)
    {
        var timer = this.DispatcherQueue.CreateTimer();
        timer.Interval = interval;
        timer.IsRepeating = repeating;
        timer.Tick += (_, _) => tick();
        return timer;
    }

    private void WireList()
    {
        this.List.ContainerContentChanging += this.contentChangingHandler;
        this.List.SelectionChanged += this.OnListSelectionChanged;
        this.List.DoubleTapped += (_, _) => this.ShowDetails();
        this.List.ContextRequested += this.OnListContextRequested;
        this.List.Loaded += (_, _) => this.ApplyScrollMode();
        this.RowMenu.Opening += (_, _) => this.PrepareRowMenu();
        this.WireRowMenu();
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.isLoaded = true;
        this.brushes = ConsoleBrushes.Resolve(this.Foreground ?? new SolidColorBrush(Colors.Gray));
        this.columns = this.columns with { CharWidth = this.MeasureCharWidth() };

        if (this.scrollViewer is null && this.List.FindDescendant<ScrollViewer>() is { } viewer)
        {
            this.scrollViewer = viewer;
            viewer.ViewChanged += this.OnScrollViewerViewChanged;
        }

        if (!this.levelsWired)
        {
            this.InitializeLevelItems();
            this.levelsWired = true;
        }
        else
        {
            this.SyncLevelMenuChecks();
        }

        this.AttachSource(this.ItemsSource);
        this.ApplyLayerState();
        this.ApplyViewOptions();
        this.ApplyScrollMode();
        this.SyncSearchModeUi();
        this.OnIsDetailsPaneOpenChanged(this.IsDetailsPaneOpen);
        this.RebuildView();
        if (this.IsPaused)
        {
            this.pauseTimer.Start();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        this.isLoaded = false;
        this.pauseTimer.Stop();
        this.searchTimer.Stop();
        this.DetachSource(this.ItemsSource);
    }

    private void OnActualThemeChanged(FrameworkElement sender, object args)
    {
        if (!this.isLoaded)
        {
            return;
        }

        this.brushes = ConsoleBrushes.Resolve(this.Foreground ?? new SolidColorBrush(Colors.Gray));
        this.RefreshRealizedRows();
        this.RefreshDetails();
        this.UpdateBufferStatus();
    }

    // With its toolbar the console is a panel: commands on the base layer, rows on its content layer. Embedded
    // without it, the host's surface is the content layer.
    private void ApplyLayerState()
        => _ = VisualStateManager.GoToState(this, this.ToolbarVisibility == Visibility.Visible ? "Panel" : "Embedded", useTransitions: false);

    private double MeasureCharWidth()
    {
        const int probeLength = 10;
        var probe = new TextBlock
        {
            Text = new string('0', probeLength),
            FontFamily = this.List.FontFamily,
            FontSize = this.List.FontSize,
            FontWeight = this.List.FontWeight,
        };
        probe.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return probe.DesiredSize.Width > 0 ? probe.DesiredSize.Width / probeLength : this.columns.CharWidth;
    }

    private void ApplyViewOptions()
    {
        this.columns = this.columns with
        {
            ShowTimestamps = this.ShowTimestamps,
            ShowSubsystem = this.ShowSubsystem,
            WordWrap = this.WordWrap,
        };
        this.RefreshRealizedRows();
    }

    private void WidenSubsystemColumn(IEnumerable<OutputLogRow> rows)
    {
        var widest = this.columns.SubsystemChars;
        foreach (var row in rows)
        {
            widest = Math.Max(widest, ConsoleColumns.ShownNameChars(row.First.Subsystem));
        }

        if (widest != this.columns.SubsystemChars)
        {
            this.columns = this.columns with { SubsystemChars = widest };
            this.RefreshRealizedRows();
        }
    }

    private void OnContainerContentChanging(ListViewBase sender, ContainerContentChangingEventArgs args)
    {
        args.Handled = true;
        if (args.InRecycleQueue
            || args.ItemContainer?.ContentTemplateRoot is not OutputLogRowView view
            || args.Item is not OutputLogRow row
            || this.brushes is null)
        {
            return;
        }

        if (args.Phase == 0)
        {
            view.Bind(row, this.columns, this.brushes, this.requestDetails);
            if (this.SearchText.Length > 0)
            {
                args.RegisterUpdateCallback(1, this.contentChangingHandler);
            }
        }
        else
        {
            this.HighlightRow(view, row);
        }
    }

    private void RebindAt(int index)
    {
        if (index >= 0
            && index < this.viewItems.Count
            && this.brushes is not null
            && this.List.ContainerFromIndex(index) is ListViewItem { ContentTemplateRoot: OutputLogRowView view })
        {
            var row = this.viewItems[index];
            view.Bind(row, this.columns, this.brushes, this.requestDetails);
            this.HighlightRow(view, row);
        }
    }

    private void RefreshRealizedRows()
    {
        if (this.List.ItemsPanelRoot is not ItemsStackPanel panel || panel.FirstCacheIndex < 0)
        {
            return;
        }

        for (var i = panel.FirstCacheIndex; i <= panel.LastCacheIndex; i++)
        {
            this.RebindAt(i);
        }
    }

    private void ApplyScrollMode()
    {
        if (this.List.ItemsPanelRoot is ItemsStackPanel panel)
        {
            panel.ItemsUpdatingScrollMode = this.FollowTail
                ? ItemsUpdatingScrollMode.KeepLastItemInView
                : ItemsUpdatingScrollMode.KeepItemsInView;
        }
    }

    private void OnFollowTailChanged(bool following)
    {
        this.FollowTailChanged?.Invoke(this, new ToggleEventArgs(following));
        this.ApplyScrollMode();
        if (following && this.isLoaded)
        {
            this.ScrollToEnd();
        }
    }

    private void ScrollToEnd()
    {
        if (!this.IsScrollEnabled || this.viewItems.Count == 0)
        {
            this.awaitingScrollToEnd = false;
            return;
        }

        this.awaitingScrollToEnd = true;
        this.List.ScrollIntoView(this.viewItems[^1]);

        // When the view is already at the end, no ViewChanged confirms the scroll.
        _ = this.DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            if (this.IsNearBottom())
            {
                this.awaitingScrollToEnd = false;
            }
        });
    }

    private bool IsNearBottom()
        => this.scrollViewer is not { } viewer
            || viewer.ScrollableHeight == 0
            || viewer.ScrollableHeight - viewer.VerticalOffset <= BottomThreshold;

    private void OnScrollViewerViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
    {
        if (!this.isLoaded || e.IsIntermediate)
        {
            return;
        }

        if (this.awaitingScrollToEnd)
        {
            // The rows were replaced or the view is being moved to the end: not the user scrolling away.
            this.awaitingScrollToEnd = !this.IsNearBottom();
            return;
        }

        if (this.FollowTail && !this.IsNearBottom())
        {
            this.FollowTail = false;
        }
    }

    private void OnFocusSearchAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        _ = this.SearchBox.Focus(FocusState.Programmatic);
        this.SearchBox.FindDescendant<TextBox>()?.SelectAll();
    }
}
