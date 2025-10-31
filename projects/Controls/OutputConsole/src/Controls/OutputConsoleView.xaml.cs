// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Serilog.Events;
using Windows.Foundation;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     A UI control that displays log entries in a live, filterable, and searchable console view.
///     This partial type contains core behavior for view wiring, event handlers and UI helpers.
/// </summary>
public sealed partial class OutputConsoleView
{
    private const double BottomThreshold = 32.0;

    // Centralized comparison for user-entered text filters: culture-aware, case-insensitive.
    private const StringComparison FilterComparison = StringComparison.CurrentCultureIgnoreCase;

    private readonly ObservableCollection<OutputLogEntry> viewItems = [];
    private Brush? accentBrush;
    private bool autoFollowSuspended; // true when user scrolled away from bottom while FollowTail is on
    private TypedEventHandler<ListViewBase, ContainerContentChangingEventArgs>? contentChangingHandler;
    private Brush? errorBrush;
    private bool isLoaded;
    private ScrollViewer? scrollViewer;
    private Brush? tertiaryBrush;
    private Brush? warningBrush;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputConsoleView" /> class.
    /// </summary>
    /// <remarks>
    ///     The constructor initializes the component and registers handlers for Loaded, Unloaded
    ///     and theme changes. Most heavy initialization is performed when the control raises
    ///     the <see cref="FrameworkElement.Loaded" /> event.
    /// </remarks>
    public OutputConsoleView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.ActualThemeChanged += this.OnActualThemeChanged;
    }

    private static T? FindDescendant<T>(DependencyObject root, Func<FrameworkElement, bool>? predicate = null)
        where T : FrameworkElement
    {
        var count = VisualTreeHelper.GetChildrenCount(root);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is FrameworkElement fe)
            {
                if (fe is T t && (predicate is null || predicate(fe)))
                {
                    return t;
                }

                var found = FindDescendant<T>(fe, predicate);
                if (found is not null)
                {
                    return found;
                }
            }
        }

        return null;
    }

    // Return a new brush that visually dims the provided brush by applying
    // opacity. For SolidColorBrush we create a new instance so we don't
    // mutate shared theme resources.
    private static Brush DimBrush(Brush brush)
    {
        if (brush is SolidColorBrush scb)
        {
            // Prefer a modest dim amount that still preserves legibility.
            const double dimOpacity = 0.5;
            return new SolidColorBrush(scb.Color)
            {
                Opacity = scb.Opacity * dimOpacity,
            };
        }

        // Non-solid brushes: fall back to returning the original brush.
        return brush;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.InitializeLoadedState();
        this.BindListAndScrollViewer();
        this.RegisterContainerContentChangingHandler();

        // ItemsSource is attached via OnItemsSourceChanged; avoid double subscription here
        this.RebuildView();

        this.RegisterControlEventHandlers();
        this.InitializeLevelItems();
        this.RegisterSearchBoxHandler();

        // Apply options initially
        this.ApplyInitialViewOptions();
    }

    private void InitializeLoadedState()
    {
        this.isLoaded = true;

        // Resolve theme resources upfront (cached)
        this.ResolveThemeResources();
    }

    private void BindListAndScrollViewer()
    {
        // Bind the ListView to a UI-thread-bound proxy collection
        this.List.ItemsSource = this.viewItems;
        this.scrollViewer ??= FindDescendant<ScrollViewer>(this.List);
        if (this.scrollViewer is not null)
        {
            this.scrollViewer.ViewChanged += this.OnScrollViewerViewChanged;
        }
    }

    private void RegisterContainerContentChangingHandler()
    {
        // Update only the realized container that changes
        this.contentChangingHandler = (sender, args) =>
        {
            if (args.ItemContainer is ListViewItem container)
            {
                var ts = FindDescendant<TextBlock>(
                    container,
                    n => string.Equals(n.Name, "TimestampBlock", StringComparison.Ordinal));
                _ = ts?.Visibility = this.ShowTimestamps ? Visibility.Visible : Visibility.Collapsed;

                var msg = FindDescendant<TextBlock>(
                    container,
                    n => string.Equals(n.Name, "MessageBlock", StringComparison.Ordinal));
                if (msg is not null)
                {
                    msg.TextWrapping = this.WordWrap ? TextWrapping.Wrap : TextWrapping.NoWrap;
                    if (args.Item is OutputLogEntry entry)
                    {
                        this.ApplyHighlight(msg, entry);
                    }
                }
            }
        };
        this.List.ContainerContentChanging += this.contentChangingHandler;
    }

    private void RegisterControlEventHandlers()
    {
        this.ClearButton.Click += (_, _) =>
        {
            if (this.ItemsSource is OutputLogBuffer buffer)
            {
                buffer.Clear();
            }

            this.ClearRequested?.Invoke(this, EventArgs.Empty);
        };

        this.FollowTailToggle.Checked += (_, _) =>
        {
            // If we're not near the bottom, don't yank the user; suspend auto-follow until near bottom
            this.autoFollowSuspended = !this.IsNearBottom();
            this.FollowTailChanged?.Invoke(this, new ToggleEventArgs(isOn: true));
        };
        this.FollowTailToggle.Unchecked += (_, _) =>
        {
            this.autoFollowSuspended = false;
            this.FollowTailChanged?.Invoke(this, new ToggleEventArgs(isOn: false));
        };

        this.PauseToggle.Checked += (_, _) =>
        {
            if (this.ItemsSource is OutputLogBuffer buffer)
            {
                buffer.IsPaused = true;
            }

            this.PauseChanged?.Invoke(this, new ToggleEventArgs(isOn: true));
        };
        this.PauseToggle.Unchecked += (_, _) =>
        {
            if (this.ItemsSource is OutputLogBuffer buffer)
            {
                buffer.IsPaused = false;
            }

            this.PauseChanged?.Invoke(this, new ToggleEventArgs(isOn: false));
        };
    }

    private void InitializeLevelItems()
    {
        // Apply sensible defaults only if DP still at framework default and still All.
        if (this.ReadLocalValue(LevelFilterProperty) == DependencyProperty.UnsetValue &&
            this.LevelFilter == LevelMask.All)
        {
            this.LevelFilter = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;
        }

        void Wire(ToggleMenuFlyoutItem item, LevelMask flag)
        {
            item.IsChecked = (this.LevelFilter & flag) != LevelMask.None;
            item.Click += (_, _) =>
            {
                this.SetLevelFlag(flag, item.IsChecked);
                this.SyncLevelMenuChecks();
                this.UpdateLevelsSummary();
            };
        }

        Wire(this.LevelVerboseItem, LevelMask.Verbose);
        Wire(this.LevelDebugItem, LevelMask.Debug);
        Wire(this.LevelInformationItem, LevelMask.Information);
        Wire(this.LevelWarningItem, LevelMask.Warning);
        Wire(this.LevelErrorItem, LevelMask.Error);
        Wire(this.LevelFatalItem, LevelMask.Fatal);

        this.UpdateLevelsSummary();
    }

    private void SyncLevelMenuChecks()
    {
        this.LevelVerboseItem.IsChecked = (this.LevelFilter & LevelMask.Verbose) != LevelMask.None;
        this.LevelDebugItem.IsChecked = (this.LevelFilter & LevelMask.Debug) != LevelMask.None;
        this.LevelInformationItem.IsChecked = (this.LevelFilter & LevelMask.Information) != LevelMask.None;
        this.LevelWarningItem.IsChecked = (this.LevelFilter & LevelMask.Warning) != LevelMask.None;
        this.LevelErrorItem.IsChecked = (this.LevelFilter & LevelMask.Error) != LevelMask.None;
        this.LevelFatalItem.IsChecked = (this.LevelFilter & LevelMask.Fatal) != LevelMask.None;
    }

    private void RegisterSearchBoxHandler() =>
        this.SearchBox.TextChanged += (_, e2) =>
        {
            if (e2.Reason == AutoSuggestionBoxTextChangeReason.UserInput)
            {
                this.TextFilter = this.SearchBox.Text;
                this.RefreshFilter();
            }
        };

    private void ApplyInitialViewOptions()
    {
        this.ApplyViewOptions();
        this.UpdateLevelsSummary();
    }

    [SuppressMessage(
        "Maintainability",
        "MA0051:Method is too long",
        Justification = "already the smallest possible without artificial splitting")]
    private void OnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (!this.isLoaded)
        {
            return;
        }

        // Marshal changes to UI thread and mirror them into the proxy collection
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            if (!this.isLoaded)
            {
                return;
            }

            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    foreach (var item in e.NewItems!.OfType<OutputLogEntry>())
                    {
                        if (this.PassesFilter(item))
                        {
                            this.viewItems.Add(item);
                        }
                    }

                    if (this.FollowTail && this.ShouldAutoScroll())
                    {
                        this.ScrollToBottom();
                    }

                    break;
                case NotifyCollectionChangedAction.Remove:
                    foreach (var item in e.OldItems!.OfType<OutputLogEntry>())
                    {
                        // Remove the first matching instance from the view proxy
                        var idx = this.viewItems.IndexOf(item);
                        if (idx >= 0)
                        {
                            this.viewItems.RemoveAt(idx);
                        }
                        else
                        {
                            // Fallback: if reference differs, remove from head to mirror ring trim
                            if (this.viewItems.Count > 0)
                            {
                                this.viewItems.RemoveAt(0);
                            }
                        }
                    }

                    break;
                case NotifyCollectionChangedAction.Reset:
                    this.RebuildView();
                    break;
                case NotifyCollectionChangedAction.Replace:
                case NotifyCollectionChangedAction.Move:
                    // For simplicity, rebuild on complex changes
                    this.RebuildView();
                    break;
            }
        });
    }

    private void AttachCollectionChanged(IEnumerable? items)
    {
        if (items is INotifyCollectionChanged incc)
        {
            incc.CollectionChanged += this.OnCollectionChanged;
        }
    }

    private void DetachCollectionChanged(IEnumerable? items)
    {
        if (items is INotifyCollectionChanged incc)
        {
            incc.CollectionChanged -= this.OnCollectionChanged;
        }
    }

    private void ApplyViewOptions()
    {
        // Reflect show timestamps and word wrap by updating container style
        for (var i = 0; i < this.List.Items.Count; i++)
        {
            if (this.List.ContainerFromIndex(i) is ListViewItem container)
            {
                var ts = FindDescendant<TextBlock>(
                    container,
                    n => string.Equals(n.Name, "TimestampBlock", StringComparison.Ordinal));
                _ = ts?.Visibility = this.ShowTimestamps ? Visibility.Visible : Visibility.Collapsed;

                var msg = FindDescendant<TextBlock>(
                    container,
                    n => string.Equals(n.Name, "MessageBlock", StringComparison.Ordinal));
                _ = msg?.TextWrapping = this.WordWrap ? TextWrapping.Wrap : TextWrapping.NoWrap;
            }
        }
    }

    private void RefreshFilter() => this.RebuildView();

    private void SetLevelFlag(LevelMask flag, bool on)
    {
        var before = this.LevelFilter;
        this.LevelFilter = on ? before | flag : before & ~flag;
        if (this.LevelFilter != before)
        {
            this.RefreshFilter();
        }
    }

    private void RebuildView()
    {
        if (!this.isLoaded)
        {
            return;
        }

        this.viewItems.Clear();
        if (this.ItemsSource is IEnumerable<OutputLogEntry> src)
        {
            foreach (var item in src)
            {
                if (this.PassesFilter(item))
                {
                    this.viewItems.Add(item);
                }
            }
        }

        if (this.ShouldAutoScroll())
        {
            this.ScrollToBottom();
        }
    }

    private bool PassesFilter(OutputLogEntry item)
    {
        // Level filter
        var levelFlag = item.Level switch
        {
            LogEventLevel.Verbose => LevelMask.Verbose,
            LogEventLevel.Debug => LevelMask.Debug,
            LogEventLevel.Information => LevelMask.Information,
            LogEventLevel.Warning => LevelMask.Warning,
            LogEventLevel.Error => LevelMask.Error,
            LogEventLevel.Fatal => LevelMask.Fatal,
            _ => LevelMask.All,
        };
        if ((this.LevelFilter & levelFlag) == LevelMask.None)
        {
            return false;
        }

        // Text filter (case-insensitive substring)
        var filter = this.TextFilter.Trim();
        if (string.IsNullOrEmpty(filter))
        {
            return true;
        }

        // Use culture-aware comparisons for user-visible text.
        if (item.Message.Contains(filter, FilterComparison))
        {
            return true;
        }

        if (item.Source?.Contains(filter, FilterComparison) == true)
        {
            return true;
        }

        if (item.Channel?.Contains(filter, FilterComparison) == true)
        {
            return true;
        }

        if (item.Exception is null)
        {
            return false;
        }

        if (item.Exception.Message.Contains(filter, FilterComparison))
        {
            return true;
        }

        // ToString() may be large; avoid multiple calls
        var exText = item.Exception.ToString();
        return exText.Contains(filter, FilterComparison);
    }

    private bool ShouldAutoScroll()
    {
        if (!this.FollowTail)
        {
            return false;
        }

        // Auto scroll only when user is near the bottom, or when not suspended
        // If suspended due to user scroll, wait until they come near bottom again
        return !this.autoFollowSuspended && this.IsNearBottom();
    }

    private void ScrollToBottom()
    {
        var last = this.viewItems.LastOrDefault();
        if (last is null)
        {
            return;
        }

        // A single, conservative strategy:
        // 1) Hint with ListView.ScrollIntoView (non-throwing in normal cases).
        // 2) Resolve the inner ScrollViewer (cached) and schedule one asynchronous
        //    attempt to call ChangeView after layout has completed. This avoids
        //    performing view changes during a layout pass which can cause
        //    re-entrant layout exceptions in WinUI.
        try
        {
            this.List.ScrollIntoView(last);
        }
        catch (Exception ex)
        {
            // Don't let a ScrollIntoView failure crash the control; surface to debug only.
            System.Diagnostics.Debug.WriteLine($"List.ScrollIntoView threw: {ex}");
        }

        // Ensure we have the ScrollViewer reference; try to find and cache it.
        this.scrollViewer ??= FindDescendant<ScrollViewer>(this.List);

        // Schedule a single safe attempt on the dispatcher so we don't call ChangeView
        // during layout/measure. Wrapping in try/catch prevents exceptions from
        // bubbling into native layout code.
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                // Resolve again in case it wasn't available earlier
                this.scrollViewer ??= FindDescendant<ScrollViewer>(this.List);
                if (this.scrollViewer is null)
                {
                    // No ScrollViewer available; nothing else to do.
                    return;
                }

                // If there's no scrollable area yet, fallback to item-based scroll
                // (ScrollIntoView is harmless). Otherwise jump to bottom deterministically.
                if (this.scrollViewer.ScrollableHeight <= 0)
                {
                    try
                    {
                        this.List.ScrollIntoView(last);
                    }
                    catch (Exception inner)
                    {
                        System.Diagnostics.Debug.WriteLine($"Fallback ScrollIntoView failed: {inner}");
                    }

                    return;
                }

                _ = this.scrollViewer.ChangeView(
                    horizontalOffset: null,
                    verticalOffset: this.scrollViewer.ScrollableHeight,
                    zoomFactor: null,
                    disableAnimation: true);
            }
            catch (Exception ex)
            {
                // Swallow to avoid native re-entrancy crashes; surface for diagnostics.
                System.Diagnostics.Debug.WriteLine($"ScrollToBottom ChangeView failed: {ex}");
            }
        });
    }

    private bool IsNearBottom()
    {
        this.scrollViewer ??= FindDescendant<ScrollViewer>(this.List);
        if (this.scrollViewer is null)
        {
            return true; // if we cannot determine, assume near bottom
        }

        var remaining = this.scrollViewer.ScrollableHeight - this.scrollViewer.VerticalOffset;
        return remaining <= BottomThreshold || this.scrollViewer.ScrollableHeight == 0;
    }

    private void OnScrollViewerViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
    {
        if (!this.isLoaded)
        {
            return;
        }

        // If FollowTail is enabled and user scrolled away from bottom, suspend auto-follow
        if (!this.FollowTail)
        {
            return;
        }

        // Resume auto-follow when near bottom
        this.autoFollowSuspended = !this.IsNearBottom();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        this.isLoaded = false;
        if (this.contentChangingHandler is not null)
        {
            this.List.ContainerContentChanging -= this.contentChangingHandler;
            this.contentChangingHandler = null;
        }

        if (this.scrollViewer is not null)
        {
            this.scrollViewer.ViewChanged -= this.OnScrollViewerViewChanged;
        }

        this.DetachCollectionChanged(this.ItemsSource);
        this.ActualThemeChanged -= this.OnActualThemeChanged; // safety
    }

    [SuppressMessage(
        "Maintainability",
        "MA0051:Method is too long",
        Justification = "already the smallest possible without artificial splitting")]
    private void ApplyHighlight(TextBlock textBlock, OutputLogEntry item)
    {
        textBlock.Inlines.Clear();

        var text = item.Message;
        var filter = this.TextFilter.Trim();

        // Use cached brushes with fallback to default
        var defaultBrush = textBlock.Foreground;
        var tertiary = this.tertiaryBrush ?? defaultBrush;
        var warning = this.warningBrush ?? defaultBrush;
        var error = this.errorBrush ?? defaultBrush;
        var accent = this.accentBrush ?? tertiary; // accent fallback to tertiary (subtle highlight)

        var prefixText = PrefixText(item);
        var prefixBrush = PrefixBrush(item);

        if (!string.IsNullOrEmpty(prefixText))
        {
            textBlock.Inlines.Add(new Run
            {
                Text = prefixText,
                Foreground = prefixBrush,
                FontFamily = textBlock.FontFamily,
            });
        }

        // For Verbose/Debug, gray-out the entire message text by using the
        // normal text color but applying opacity (dim). This keeps the same
        // hue on both Light and Dark themes while making verbose/debug less
        // visually prominent.
        // For dimming verbose/debug we should base the dim on the actual
        // rendered foreground (defaultBrush) so that the hue matches the
        // current theme. Using `tertiary` here could be a theme resource
        // that doesn't match the control foreground in all cases.
        var messageBaseBrush = item.Level is LogEventLevel.Verbose or LogEventLevel.Debug
            ? DimBrush(defaultBrush)
            : defaultBrush;

        if (string.IsNullOrEmpty(filter))
        {
            textBlock.Inlines.Add(new Run
            {
                Text = text,
                Foreground = messageBaseBrush,
                FontFamily = textBlock.FontFamily,
            });
            return;
        }

        var idx = 0;
        while (idx < text.Length)
        {
            var matchIndex = text.IndexOf(filter, idx, FilterComparison);
            if (matchIndex < 0)
            {
                if (idx < text.Length)
                {
                    textBlock.Inlines.Add(new Run
                    {
                        Text = text[idx..],
                        Foreground = messageBaseBrush,
                        FontFamily = textBlock.FontFamily,
                    });
                }

                break;
            }

            if (matchIndex > idx)
            {
                textBlock.Inlines.Add(new Run
                {
                    Text = text[idx..matchIndex],
                    Foreground = messageBaseBrush,
                    FontFamily = textBlock.FontFamily,
                });
            }

            var matchRun = new Run
            {
                Text = text[matchIndex..(matchIndex + filter.Length)],
                FontFamily = textBlock.FontFamily,
                Foreground = accent,
            };
            textBlock.Inlines.Add(matchRun);

            idx = matchIndex + filter.Length;
        }

        return;

        string PrefixText(OutputLogEntry outputLogEntry)
        {
            // Build level prefix like [I] with color per level
            return outputLogEntry.Level switch
            {
                LogEventLevel.Verbose => "[V] ",
                LogEventLevel.Debug => "[D] ",
                LogEventLevel.Information => "[I] ",
                LogEventLevel.Warning => "[W] ",
                LogEventLevel.Error => "[E] ",
                LogEventLevel.Fatal => "[F] ",
                _ => string.Empty,
            };
        }

        Brush PrefixBrush(OutputLogEntry item1)
        {
            return item1.Level switch
            {
                LogEventLevel.Verbose => DimBrush(defaultBrush),
                LogEventLevel.Debug => DimBrush(defaultBrush),
                LogEventLevel.Information => defaultBrush,
                LogEventLevel.Warning => warning,
                LogEventLevel.Error => error,
                LogEventLevel.Fatal => error,
                _ => defaultBrush,
            };
        }
    }

    private void ResolveThemeResources()
    {
        var resources = Application.Current?.Resources;
        if (resources is null)
        {
            this.tertiaryBrush = null;
            this.warningBrush = null;
            this.errorBrush = null;
            this.accentBrush = null;
            return;
        }

        // Use the primary text fill as the base for "tertiary" readable text
        // and apply opacity when rendering verbose/debug messages. This keeps
        // the hue identical across themes while allowing dimming via Opacity.
        this.tertiaryBrush = TryGet("TextFillColorPrimaryBrush");
        this.warningBrush = TryGet("SystemFillColorCautionBrush");
        this.errorBrush = TryGet("SystemFillColorCriticalBrush");
        this.accentBrush = TryGet("AccentTextFillColorPrimaryBrush");
        return;

        Brush? TryGet(string key)
        {
            return resources.TryGetValue(key, out var obj) && obj is Brush b ? b : null;
        }
    }

    private void OnActualThemeChanged(FrameworkElement sender, object args)
    {
        this.ResolveThemeResources();
        this.RefreshHighlights();
    }

    private void RefreshHighlights()
    {
        if (!this.isLoaded)
        {
            return;
        }

        // Re-apply highlight to realized containers only
        var count = this.List.Items.Count;
        for (var i = 0; i < count; i++)
        {
            if (this.List.ContainerFromIndex(i) is not ListViewItem container)
            {
                continue;
            }

            var msg = FindDescendant<TextBlock>(
                container,
                n => string.Equals(n.Name, "MessageBlock", StringComparison.Ordinal));
            if (msg is not null && this.List.Items[i] is OutputLogEntry entry)
            {
                this.ApplyHighlight(msg, entry);
            }
        }
    }

    private void UpdateLevelsSummary()
    {
        // Cannot stackalloc tuples containing reference types (string). Use a managed array instead.
        var levels = new (LevelMask flag, string name)[]
        {
            (LevelMask.Verbose, "Verbose"), (LevelMask.Debug, "Debug"), (LevelMask.Information, "Information"),
            (LevelMask.Warning, "Warning"), (LevelMask.Error, "Error"), (LevelMask.Fatal, "Fatal"),
        };

        var selectedCount = 0;
        foreach (var (flag, _) in levels)
        {
            if ((this.LevelFilter & flag) != LevelMask.None)
            {
                selectedCount++;
            }
        }

        var summary = selectedCount == levels.Length ? "All" : this.GetHighestSelectedLevelName();
        this.LevelsDropDown.Content = string.Format(
            CultureInfo.InvariantCulture,
            "Levels: {0} ({1}/{2})",
            summary,
            selectedCount,
            levels.Length);
    }

    private string GetHighestSelectedLevelName() =>
        this.LevelFilter switch
        {
            // Highest severity first
            var f when (f & LevelMask.Fatal) == LevelMask.Fatal => "Fatal",
            var f when (f & LevelMask.Error) == LevelMask.Error => "Error",
            var f when (f & LevelMask.Warning) == LevelMask.Warning => "Warning",
            var f when (f & LevelMask.Information) == LevelMask.Information => "Information",
            var f when (f & LevelMask.Debug) == LevelMask.Debug => "Debug",
            var f when (f & LevelMask.Verbose) == LevelMask.Verbose => "Verbose",
            _ => "None",
        };

    private void OnFindNextAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        if (!this.isLoaded || this.viewItems.Count == 0)
        {
            return;
        }

        var start = this.List.SelectedIndex;
        if (start < 0)
        {
            start = -1;
        }

        var n = this.viewItems.Count;
        var idx = (start + 1) % n;
        this.SelectAndScrollToIndex(idx);
    }

    private void OnFindPrevAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        if (!this.isLoaded || this.viewItems.Count == 0)
        {
            return;
        }

        var start = this.List.SelectedIndex;
        if (start < 0)
        {
            start = 0;
        }

        var n = this.viewItems.Count;
        var idx = (start - 1) % n;
        if (idx < 0)
        {
            idx += n;
        }

        this.SelectAndScrollToIndex(idx);
    }

    private void OnFocusSearchAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        _ = this.SearchBox.Focus(FocusState.Programmatic);
        var tb = FindDescendant<TextBox>(this.SearchBox);
        tb?.SelectAll();
    }

    private void SelectAndScrollToIndex(int index)
    {
        if (index < 0 || index >= this.viewItems.Count)
        {
            return;
        }

        var item = this.viewItems[index];
        this.List.SelectedItem = item;
        this.SelectedItem = item;
        this.List.ScrollIntoView(item);
    }
}
