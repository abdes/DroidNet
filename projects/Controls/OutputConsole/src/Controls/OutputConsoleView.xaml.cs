// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Serilog.Events;
using Windows.Foundation;

namespace DroidNet.Controls.OutputConsole;

public sealed partial class OutputConsoleView : UserControl
{
    private const double BottomThreshold = 32.0;
    private readonly ObservableCollection<OutputLogEntry> _viewItems = new();
    private bool _autoFollowSuspended; // true when user scrolled away from bottom while FollowTail is on
    private TypedEventHandler<ListViewBase, ContainerContentChangingEventArgs>? _contentChangingHandler;
    private bool _isLoaded;
    private ScrollViewer? _scrollViewer;

    public OutputConsoleView()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _isLoaded = true;
        // Bind the ListView to a UI-thread-bound proxy collection
        List.ItemsSource = _viewItems;
        _scrollViewer ??= FindDescendant<ScrollViewer>(List);
        if (_scrollViewer is not null)
        {
            _scrollViewer.ViewChanged += OnScrollViewerViewChanged;
        }

        // Update only the realized container that changes
        _contentChangingHandler = (_, args) =>
        {
            if (args.ItemContainer is ListViewItem container)
            {
                var ts = FindDescendant<TextBlock>(container, n => n.Name == "TimestampBlock");
                if (ts is not null)
                {
                    ts.Visibility = ShowTimestamps ? Visibility.Visible : Visibility.Collapsed;
                }

                var msg = FindDescendant<TextBlock>(container, n => n.Name == "MessageBlock");
                if (msg is not null)
                {
                    msg.TextWrapping = WordWrap ? TextWrapping.Wrap : TextWrapping.NoWrap;
                    if (args.Item is OutputLogEntry entry)
                    {
                        ApplyHighlight(msg, entry);
                    }
                }
            }
        };
        List.ContainerContentChanging += _contentChangingHandler;
    // ItemsSource is attached via OnItemsSourceChanged; avoid double subscription here
    RebuildView();
        ClearButton.Click += (_, __) =>
        {
            if (ItemsSource is OutputLogBuffer buffer)
            {
                buffer.Clear();
            }

            ClearRequested?.Invoke(this, EventArgs.Empty);
        };
        FollowTailToggle.Checked += (_, __) =>
        {
            FollowTail = true;
            // If we're not near the bottom, don't yank the user; suspend auto-follow until near bottom
            _autoFollowSuspended = !IsNearBottom();
            FollowTailChanged?.Invoke(this, true);
        };
        FollowTailToggle.Unchecked += (_, __) =>
        {
            FollowTail = false;
            _autoFollowSuspended = false;
            FollowTailChanged?.Invoke(this, false);
        };
        PauseToggle.Checked += (_, __) =>
        {
            IsPaused = true;
            if (ItemsSource is OutputLogBuffer buffer)
            {
                buffer.IsPaused = true;
            }

            PauseChanged?.Invoke(this, true);
        };
        PauseToggle.Unchecked += (_, __) =>
        {
            IsPaused = false;
            if (ItemsSource is OutputLogBuffer buffer)
            {
                buffer.IsPaused = false;
            }

            PauseChanged?.Invoke(this, false);
        };

        ShowTimestampsToggle.Checked += (_, __) => ShowTimestamps = true;
        ShowTimestampsToggle.Unchecked += (_, __) => ShowTimestamps = false;
        WrapToggle.Checked += (_, __) => WordWrap = true;
        WrapToggle.Unchecked += (_, __) => WordWrap = false;
        // Levels dropdown items
        // Initialize sensible defaults if filter is at All
        if (LevelFilter == LevelMask.All)
        {
            LevelFilter = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;
        }

        LevelVerboseItem.IsChecked = (LevelFilter & LevelMask.Verbose) != 0;
        LevelDebugItem.IsChecked = (LevelFilter & LevelMask.Debug) != 0;
        LevelInformationItem.IsChecked = (LevelFilter & LevelMask.Information) != 0;
        LevelWarningItem.IsChecked = (LevelFilter & LevelMask.Warning) != 0;
        LevelErrorItem.IsChecked = (LevelFilter & LevelMask.Error) != 0;
        LevelFatalItem.IsChecked = (LevelFilter & LevelMask.Fatal) != 0;

        LevelVerboseItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Verbose, LevelVerboseItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        LevelDebugItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Debug, LevelDebugItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        LevelInformationItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Information, LevelInformationItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        LevelWarningItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Warning, LevelWarningItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        LevelErrorItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Error, LevelErrorItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        LevelFatalItem.Click += (_, __) =>
        {
            SetLevelFlag(LevelMask.Fatal, LevelFatalItem.IsChecked);
            RefreshFilter();
            UpdateLevelsSummary();
        };
        SearchBox.TextChanged += (_, e2) =>
        {
            if (e2.Reason == AutoSuggestionBoxTextChangeReason.UserInput)
            {
                TextFilter = SearchBox.Text;
                RefreshFilter();
            }
        };

        // Apply options initially
        ApplyViewOptions();
        UpdateLevelsSummary();
    }

    private void OnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (!_isLoaded)
        {
            return;
        }

        // Marshal changes to UI thread and mirror them into the proxy collection
        _ = DispatcherQueue.TryEnqueue(() =>
        {
            if (!_isLoaded)
            {
                return;
            }

            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    foreach (var item in e.NewItems!.OfType<OutputLogEntry>())
                    {
                        if (PassesFilter(item))
                        {
                            _viewItems.Add(item);
                        }
                    }

                    if (FollowTail && ShouldAutoScroll())
                    {
                        ScrollToBottom();
                    }

                    break;
                case NotifyCollectionChangedAction.Remove:
                    foreach (var item in e.OldItems!.OfType<OutputLogEntry>())
                    {
                        // Remove the first matching instance from the view proxy
                        var idx = _viewItems.IndexOf(item);
                        if (idx >= 0)
                        {
                            _viewItems.RemoveAt(idx);
                        }
                        else
                        {
                            // Fallback: if reference differs, remove from head to mirror ring trim
                            if (_viewItems.Count > 0)
                            {
                                _viewItems.RemoveAt(0);
                            }
                        }
                    }

                    break;
                case NotifyCollectionChangedAction.Reset:
                    RebuildView();
                    break;
                case NotifyCollectionChangedAction.Replace:
                case NotifyCollectionChangedAction.Move:
                    // For simplicity, rebuild on complex changes
                    RebuildView();
                    break;
            }
        });
    }

    private void AttachCollectionChanged(IEnumerable? items)
    {
        if (items is INotifyCollectionChanged incc)
        {
            incc.CollectionChanged += OnCollectionChanged;
        }
    }

    private void DetachCollectionChanged(IEnumerable? items)
    {
        if (items is INotifyCollectionChanged incc)
        {
            incc.CollectionChanged -= OnCollectionChanged;
        }
    }

    private void ApplyViewOptions()
    {
        // Reflect show timestamps and word wrap by updating container style
        for (var i = 0; i < List.Items.Count; i++)
        {
            if (List.ContainerFromIndex(i) is ListViewItem container)
            {
                var ts = FindDescendant<TextBlock>(container, n => n.Name == "TimestampBlock");
                if (ts is not null)
                {
                    ts.Visibility = ShowTimestamps ? Visibility.Visible : Visibility.Collapsed;
                }

                var msg = FindDescendant<TextBlock>(container, n => n.Name == "MessageBlock");
                if (msg is not null)
                {
                    msg.TextWrapping = WordWrap ? TextWrapping.Wrap : TextWrapping.NoWrap;
                }
            }
        }
    }

    private void RefreshFilter() => RebuildView();

    private void SetLevelFlag(LevelMask flag, bool on)
    {
        LevelFilter = on ? this.LevelFilter | flag : this.LevelFilter & ~flag;
        RefreshFilter();
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

    private void RebuildView()
    {
        if (!_isLoaded)
        {
            return;
        }

        _viewItems.Clear();
        if (ItemsSource is IEnumerable<OutputLogEntry> src)
        {
            foreach (var item in src)
            {
                if (PassesFilter(item))
                {
                    _viewItems.Add(item);
                }
            }
        }

        if (ShouldAutoScroll())
        {
            ScrollToBottom();
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
        if ((LevelFilter & levelFlag) == 0)
        {
            return false;
        }

        // Text filter (case-insensitive substring)
        var filter = TextFilter;
        if (string.IsNullOrWhiteSpace(filter))
        {
            return true;
        }

        var comparison = StringComparison.OrdinalIgnoreCase;
        if (item.Message?.IndexOf(filter, comparison) >= 0)
        {
            return true;
        }

        if (item.Source?.IndexOf(filter, comparison) >= 0)
        {
            return true;
        }

        if (item.Channel?.IndexOf(filter, comparison) >= 0)
        {
            return true;
        }

        if (item.Exception is not null)
        {
            if (item.Exception.Message?.IndexOf(filter, comparison) >= 0)
            {
                return true;
            }

            if (item.Exception.ToString().IndexOf(filter, comparison) >= 0)
            {
                return true;
            }
        }

        return false;
    }

    private bool ShouldAutoScroll()
    {
        if (!FollowTail)
        {
            return false;
        }

        // Auto scroll only when user is near the bottom, or when not suspended
        // If suspended due to user scroll, wait until they come near bottom again
        return !_autoFollowSuspended && IsNearBottom();
    }

    private void ScrollToBottom()
    {
        var last = _viewItems.LastOrDefault();
        if (last is null)
        {
            return;
        }

        // First try standard item-based scroll
        List.ScrollIntoView(last);

        // Schedule another attempt after layout/realization
        _ = DispatcherQueue.TryEnqueue(() => List.ScrollIntoView(last));

        // Also try directly scrolling the underlying ScrollViewer
        _scrollViewer ??= FindDescendant<ScrollViewer>(List);
        if (_scrollViewer is not null)
        {
            // Jump to bottom; disable animation to make it deterministic
            _ = _scrollViewer.ChangeView(null, _scrollViewer.ScrollableHeight, null, true);
        }
    }

    private bool IsNearBottom()
    {
        _scrollViewer ??= FindDescendant<ScrollViewer>(List);
        if (_scrollViewer is null)
        {
            return true; // if we cannot determine, assume near bottom
        }

        var remaining = _scrollViewer.ScrollableHeight - _scrollViewer.VerticalOffset;
        return remaining <= BottomThreshold || _scrollViewer.ScrollableHeight == 0;
    }

    private void OnScrollViewerViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
    {
        if (!_isLoaded)
        {
            return;
        }

        // If FollowTail is enabled and user scrolled away from bottom, suspend auto-follow
        if (FollowTail)
        {
            if (IsNearBottom())
            {
                // Resume auto-follow when near bottom
                _autoFollowSuspended = false;
            }
            else
            {
                _autoFollowSuspended = true;
            }
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _isLoaded = false;
        if (_contentChangingHandler is not null)
        {
            List.ContainerContentChanging -= _contentChangingHandler;
            _contentChangingHandler = null;
        }

        if (_scrollViewer is not null)
        {
            _scrollViewer.ViewChanged -= OnScrollViewerViewChanged;
        }

        DetachCollectionChanged(ItemsSource);
    }

    #region Dependency Properties

    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource), typeof(IEnumerable), typeof(OutputConsoleView),
        new PropertyMetadata(null, OnItemsSourceChanged));

    public IEnumerable? ItemsSource
    {
        get => (IEnumerable?)GetValue(ItemsSourceProperty);
        set => SetValue(ItemsSourceProperty, value);
    }

    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.DetachCollectionChanged(e.OldValue as IEnumerable);
        // Keep ListView bound to proxy; just rebuild its content from the new source
        v.AttachCollectionChanged(e.NewValue as IEnumerable);
        v.RebuildView();
    }

    public static readonly DependencyProperty FollowTailProperty = DependencyProperty.Register(
        nameof(FollowTail), typeof(bool), typeof(OutputConsoleView), new PropertyMetadata(true));

    public bool FollowTail
    {
        get => (bool)GetValue(FollowTailProperty);
        set => SetValue(FollowTailProperty, value);
    }

    public static readonly DependencyProperty IsPausedProperty = DependencyProperty.Register(
        nameof(IsPaused), typeof(bool), typeof(OutputConsoleView), new PropertyMetadata(false));

    public bool IsPaused
    {
        get => (bool)GetValue(IsPausedProperty);
        set => SetValue(IsPausedProperty, value);
    }

    public static readonly DependencyProperty ShowTimestampsProperty = DependencyProperty.Register(
        nameof(ShowTimestamps), typeof(bool), typeof(OutputConsoleView),
        new PropertyMetadata(false, OnViewOptionChanged));

    public bool ShowTimestamps
    {
        get => (bool)GetValue(ShowTimestampsProperty);
        set => SetValue(ShowTimestampsProperty, value);
    }

    public static readonly DependencyProperty WordWrapProperty = DependencyProperty.Register(
        nameof(WordWrap), typeof(bool), typeof(OutputConsoleView), new PropertyMetadata(false, OnViewOptionChanged));

    public bool WordWrap
    {
        get => (bool)GetValue(WordWrapProperty);
        set => SetValue(WordWrapProperty, value);
    }

    public static readonly DependencyProperty TextFilterProperty = DependencyProperty.Register(
        nameof(TextFilter), typeof(string), typeof(OutputConsoleView),
        new PropertyMetadata(string.Empty, OnFilterChanged));

    public string TextFilter
    {
        get => (string)GetValue(TextFilterProperty);
        set => SetValue(TextFilterProperty, value);
    }

    public static readonly DependencyProperty LevelFilterProperty = DependencyProperty.Register(
        nameof(LevelFilter), typeof(LevelMask), typeof(OutputConsoleView),
        new PropertyMetadata(LevelMask.All, OnFilterChanged));

    public LevelMask LevelFilter
    {
        get => (LevelMask)GetValue(LevelFilterProperty);
        set => SetValue(LevelFilterProperty, value);
    }

    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem), typeof(OutputLogEntry), typeof(OutputConsoleView), new PropertyMetadata(null));

    public OutputLogEntry? SelectedItem
    {
        get => (OutputLogEntry?)GetValue(SelectedItemProperty);
        set => SetValue(SelectedItemProperty, value);
    }

    private static void OnViewOptionChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.ApplyViewOptions();
    }

    private static void OnFilterChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.RefreshFilter();
    }

    #endregion

    #region Events

    public event EventHandler? ClearRequested;
    public event EventHandler<bool>? FollowTailChanged;
    public event EventHandler<bool>? PauseChanged;

    #endregion

    #region Helpers: Highlight, Levels summary, Accelerators

    private void ApplyHighlight(TextBlock textBlock, OutputLogEntry item)
    {
        var text = item.Message ?? string.Empty;
        var filter = TextFilter;
        textBlock.Inlines.Clear();

        // Theme-aware brushes
        Brush GetThemeBrush(string resourceKey, Brush fallback)
        {
            if (Application.Current?.Resources is not null &&
                Application.Current.Resources.TryGetValue(resourceKey, out var obj) && obj is Brush br)
            {
                return br;
            }

            return fallback;
        }

        var defaultBrush = textBlock.Foreground;
        var tertiary = GetThemeBrush("TextFillColorTertiaryBrush", defaultBrush);
        var warning = GetThemeBrush("SystemFillColorCautionBrush", defaultBrush);
        var error = GetThemeBrush("SystemFillColorCriticalBrush", defaultBrush);
        Brush? accentBrush = null;
        if (Application.Current?.Resources is not null &&
            Application.Current.Resources.TryGetValue("AccentTextFillColorPrimaryBrush", out var accentObj) &&
            accentObj is Brush b)
        {
            accentBrush = b;
        }

        // Build level prefix like [I] with color per level
        var prefixText = item.Level switch
        {
            LogEventLevel.Verbose => "[V] ",
            LogEventLevel.Debug => "[D] ",
            LogEventLevel.Information => "[I] ",
            LogEventLevel.Warning => "[W] ",
            LogEventLevel.Error => "[E] ",
            LogEventLevel.Fatal => "[F] ",
            _ => "",
        };

        var prefixBrush = item.Level switch
        {
            LogEventLevel.Verbose => tertiary,
            LogEventLevel.Debug => tertiary,
            LogEventLevel.Information => defaultBrush,
            LogEventLevel.Warning => warning,
            LogEventLevel.Error => error,
            LogEventLevel.Fatal => error,
            _ => defaultBrush,
        };

        if (!string.IsNullOrEmpty(prefixText))
        {
            textBlock.Inlines.Add(new Run
            {
                Text = prefixText,
                Foreground = prefixBrush,
                FontFamily = textBlock.FontFamily,
            });
        }

        // For Verbose/Debug, gray-out the entire message text
        var messageBaseBrush = item.Level == LogEventLevel.Verbose || item.Level == LogEventLevel.Debug
            ? tertiary
            : defaultBrush;

        if (string.IsNullOrWhiteSpace(filter))
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
        var comparison = StringComparison.OrdinalIgnoreCase;
        while (idx < text.Length)
        {
            var matchIndex = text.IndexOf(filter, idx, comparison);
            if (matchIndex < 0)
            {
                if (idx < text.Length)
                {
                    textBlock.Inlines.Add(new Run
                    {
                        Text = text.Substring(idx),
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
                    Text = text.Substring(idx, matchIndex - idx),
                    Foreground = messageBaseBrush,
                    FontFamily = textBlock.FontFamily,
                });
            }

            var matchRun = new Run
            {
                Text = text.Substring(matchIndex, filter.Length),
                FontFamily = textBlock.FontFamily,
            };
            if (accentBrush is not null)
            {
                matchRun.Foreground = accentBrush;
            }
            else
            {
                matchRun.Foreground = messageBaseBrush;
            }

            textBlock.Inlines.Add(matchRun);

            idx = matchIndex + filter.Length;
        }
    }

    private void UpdateLevelsSummary()
    {
        // Count selected levels
        var levels = new (LevelMask Flag, string Name)[]
        {
            (LevelMask.Verbose, "Verbose"), (LevelMask.Debug, "Debug"), (LevelMask.Information, "Information"),
            (LevelMask.Warning, "Warning"), (LevelMask.Error, "Error"), (LevelMask.Fatal, "Fatal"),
        };

        var selectedCount = levels.Count(l => (LevelFilter & l.Flag) != 0);
        var highest = GetHighestSelectedLevelName();
        var summary = selectedCount == levels.Length ? "All" : highest;
        LevelsDropDown.Content = $"Levels: {summary} ({selectedCount}/{levels.Length})";
    }

    private string GetHighestSelectedLevelName()
    {
        // Highest severity first
        if ((LevelFilter & LevelMask.Fatal) != 0)
        {
            return "Fatal";
        }

        if ((LevelFilter & LevelMask.Error) != 0)
        {
            return "Error";
        }

        if ((LevelFilter & LevelMask.Warning) != 0)
        {
            return "Warning";
        }

        if ((LevelFilter & LevelMask.Information) != 0)
        {
            return "Information";
        }

        if ((LevelFilter & LevelMask.Debug) != 0)
        {
            return "Debug";
        }

        if ((LevelFilter & LevelMask.Verbose) != 0)
        {
            return "Verbose";
        }

        return "None";
    }

    private void OnFindNextAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        if (!_isLoaded || _viewItems.Count == 0)
        {
            return;
        }

        var start = List.SelectedIndex;
        if (start < 0)
        {
            start = -1;
        }

        var n = _viewItems.Count;
        for (var i = 1; i <= n; i++)
        {
            var idx = (start + i) % n;
            SelectAndScrollToIndex(idx);
            return;
        }
    }

    private void OnFindPrevAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        if (!_isLoaded || _viewItems.Count == 0)
        {
            return;
        }

        var start = List.SelectedIndex;
        if (start < 0)
        {
            start = 0;
        }

        var n = _viewItems.Count;
        for (var i = 1; i <= n; i++)
        {
            var idx = (start - i) % n;
            if (idx < 0)
            {
                idx += n;
            }

            SelectAndScrollToIndex(idx);
            return;
        }
    }

    private void OnFocusSearchAccelerator(object sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        _ = SearchBox.Focus(FocusState.Programmatic);
        var tb = FindDescendant<TextBox>(SearchBox);
        tb?.SelectAll();
    }

    private void SelectAndScrollToIndex(int index)
    {
        if (index < 0 || index >= _viewItems.Count)
        {
            return;
        }

        var item = _viewItems[index];
        List.SelectedItem = item;
        SelectedItem = item;
        List.ScrollIntoView(item);
    }

    #endregion
}
