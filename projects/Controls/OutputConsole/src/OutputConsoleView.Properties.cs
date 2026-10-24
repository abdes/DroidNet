// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Partial declarations for <see cref="OutputConsoleView" /> related to
///     dependency properties and related public properties.
/// </summary>
public partial class OutputConsoleView
{
    /// <summary>Identifies whether the console owns scrolling or expands within a scrolling host.</summary>
    public static readonly DependencyProperty IsScrollEnabledProperty = DependencyProperty.Register(
        nameof(IsScrollEnabled), typeof(bool), typeof(OutputConsoleView), new PropertyMetadata(defaultValue: true, OnScrollEnabledChanged));

    /// <summary>Identifies visibility of the optional console command bar.</summary>
    public static readonly DependencyProperty ToolbarVisibilityProperty = DependencyProperty.Register(
        nameof(ToolbarVisibility), typeof(Visibility), typeof(OutputConsoleView), new PropertyMetadata(Visibility.Visible, OnToolbarVisibilityChanged));

    /// <summary>
    ///     Identifies the <see cref="TextFilter" /> dependency property.
    ///     Holds the search text: it filters or finds lines, per <see cref="SearchMode" />.
    /// </summary>
    public static readonly DependencyProperty TextFilterProperty = DependencyProperty.Register(
        nameof(TextFilter),
        typeof(string),
        typeof(OutputConsoleView),
        new PropertyMetadata(string.Empty, OnTextFilterChanged));

    /// <summary>
    ///     Identifies the <see cref="SearchMode" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty SearchModeProperty = DependencyProperty.Register(
        nameof(SearchMode),
        typeof(OutputSearchMode),
        typeof(OutputConsoleView),
        new PropertyMetadata(OutputSearchMode.Filter, OnSearchModeChanged));

    /// <summary>
    ///     Identifies the <see cref="LevelFilter" /> dependency property.
    ///     A bitmask selecting which log levels are shown.
    /// </summary>
    public static readonly DependencyProperty LevelFilterProperty = DependencyProperty.Register(
        nameof(LevelFilter),
        typeof(LevelMask),
        typeof(OutputConsoleView),
        new PropertyMetadata(LevelMask.All, OnFilterChanged));

    /// <summary>
    ///     Identifies the <see cref="HiddenSubsystems" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HiddenSubsystemsProperty = DependencyProperty.Register(
        nameof(HiddenSubsystems),
        typeof(IReadOnlyCollection<string>),
        typeof(OutputConsoleView),
        new PropertyMetadata(Array.Empty<string>(), OnFilterChanged));

    /// <summary>
    ///     Identifies the <see cref="FoldDuplicates" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty FoldDuplicatesProperty = DependencyProperty.Register(
        nameof(FoldDuplicates),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnFilterChanged));

    /// <summary>
    ///     Identifies the <see cref="SelectedItem" /> dependency property.
    ///     Represents the most recently selected <see cref="OutputLogEntry" /> in the view.
    /// </summary>
    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem),
        typeof(OutputLogEntry),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: null, OnSelectedItemChanged));

    /// <summary>
    ///     Identifies the <see cref="ItemsSource" /> dependency property.
    ///     The source collection of log entries the control will display.
    /// </summary>
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource),
        typeof(IEnumerable),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: null, OnItemsSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="FollowTail" /> dependency property.
    ///     When true, view will auto-scroll to show the latest entries.
    /// </summary>
    public static readonly DependencyProperty FollowTailProperty = DependencyProperty.Register(
        nameof(FollowTail),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: true, OnFollowTailChanged));

    /// <summary>
    ///     Identifies the <see cref="IsPaused" /> dependency property.
    ///     When true, this view stops showing new entries; the source keeps capturing them.
    /// </summary>
    public static readonly DependencyProperty IsPausedProperty = DependencyProperty.Register(
        nameof(IsPaused),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnIsPausedChanged));

    /// <summary>
    ///     Identifies the <see cref="ShowTimestamps" /> dependency property.
    ///     Controls whether timestamps are shown for each entry.
    /// </summary>
    public static readonly DependencyProperty ShowTimestampsProperty = DependencyProperty.Register(
        nameof(ShowTimestamps),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnViewOptionChanged));

    /// <summary>
    ///     Identifies the <see cref="ShowSubsystem" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ShowSubsystemProperty = DependencyProperty.Register(
        nameof(ShowSubsystem),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: true, OnViewOptionChanged));

    /// <summary>
    ///     Identifies the <see cref="WordWrap" /> dependency property.
    ///     Controls whether message text wraps in the view.
    /// </summary>
    public static readonly DependencyProperty WordWrapProperty = DependencyProperty.Register(
        nameof(WordWrap),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnViewOptionChanged));

    /// <summary>
    ///     Identifies the <see cref="IsDetailsPaneOpen" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsDetailsPaneOpenProperty = DependencyProperty.Register(
        nameof(IsDetailsPaneOpen),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnIsDetailsPaneOpenChanged));

    /// <summary>
    ///     Identifies the <see cref="DetailsSplitRatio" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty DetailsSplitRatioProperty = DependencyProperty.Register(
        nameof(DetailsSplitRatio),
        typeof(double),
        typeof(OutputConsoleView),
        new PropertyMetadata(DefaultDetailsSplitRatio, OnDetailsSplitRatioChanged));

    /// <summary>Gets or sets a value indicating whether the console scrolls internally; disable when its host owns scrolling.</summary>
    public bool IsScrollEnabled
    {
        get => (bool)this.GetValue(IsScrollEnabledProperty);
        set => this.SetValue(IsScrollEnabledProperty, value);
    }

    /// <summary>Gets or sets whether the host displays the console's command bar.</summary>
    public Visibility ToolbarVisibility
    {
        get => (Visibility)this.GetValue(ToolbarVisibilityProperty);
        set => this.SetValue(ToolbarVisibilityProperty, value);
    }

    /// <summary>
    ///     Gets or sets the search text. In <see cref="OutputSearchMode.Filter" /> mode it hides non-matching lines;
    ///     in <see cref="OutputSearchMode.Find" /> mode it highlights matches.
    /// </summary>
    public string TextFilter
    {
        get => (string)this.GetValue(TextFilterProperty);
        set => this.SetValue(TextFilterProperty, value);
    }

    /// <summary>
    ///     Gets or sets what the search text does.
    /// </summary>
    public OutputSearchMode SearchMode
    {
        get => (OutputSearchMode)this.GetValue(SearchModeProperty);
        set => this.SetValue(SearchModeProperty, value);
    }

    /// <summary>
    ///     Gets or sets the bitmask indicating which log levels are visible.
    ///     Bound to <see cref="LevelFilterProperty" />.
    /// </summary>
    public LevelMask LevelFilter
    {
        get => (LevelMask)this.GetValue(LevelFilterProperty);
        set => this.SetValue(LevelFilterProperty, value);
    }

    /// <summary>
    ///     Gets or sets the subsystems whose lines are hidden. An empty string stands for lines without a subsystem.
    ///     Subsystems that are not listed, including ones that appear later, are shown.
    /// </summary>
    public IReadOnlyCollection<string> HiddenSubsystems
    {
        get => (IReadOnlyCollection<string>)this.GetValue(HiddenSubsystemsProperty);
        set => this.SetValue(HiddenSubsystemsProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether consecutive identical lines fold into one row with a count.
    /// </summary>
    public bool FoldDuplicates
    {
        get => (bool)this.GetValue(FoldDuplicatesProperty);
        set => this.SetValue(FoldDuplicatesProperty, value);
    }

    /// <summary>
    ///     Gets or sets the most recently selected <see cref="OutputLogEntry" />. Setting it selects the line that
    ///     contains it.
    /// </summary>
    public OutputLogEntry? SelectedItem
    {
        get => (OutputLogEntry?)this.GetValue(SelectedItemProperty);
        set => this.SetValue(SelectedItemProperty, value);
    }

    /// <summary>
    ///     Gets or sets the source of log entries: an <see cref="OutputLogBuffer" />, or a collection of
    ///     <see cref="OutputLogEntry" /> changed on the UI thread.
    /// </summary>
    public IEnumerable? ItemsSource
    {
        get => (IEnumerable?)this.GetValue(ItemsSourceProperty);
        set => this.SetValue(ItemsSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the view keeps the newest line in view.
    ///     Bound to <see cref="FollowTailProperty" />.
    /// </summary>
    public bool FollowTail
    {
        get => (bool)this.GetValue(FollowTailProperty);
        set => this.SetValue(FollowTailProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether this view stops showing new entries. The source keeps capturing
    ///     them, and they appear when the view resumes.
    /// </summary>
    public bool IsPaused
    {
        get => (bool)this.GetValue(IsPausedProperty);
        set => this.SetValue(IsPausedProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether timestamps are displayed for each entry.
    ///     Bound to <see cref="ShowTimestampsProperty" />.
    /// </summary>
    public bool ShowTimestamps
    {
        get => (bool)this.GetValue(ShowTimestampsProperty);
        set => this.SetValue(ShowTimestampsProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether each line's prefix ends with its subsystem.
    /// </summary>
    public bool ShowSubsystem
    {
        get => (bool)this.GetValue(ShowSubsystemProperty);
        set => this.SetValue(ShowSubsystemProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether message text is wrapped in the UI.
    ///     Bound to <see cref="WordWrapProperty" />.
    /// </summary>
    public bool WordWrap
    {
        get => (bool)this.GetValue(WordWrapProperty);
        set => this.SetValue(WordWrapProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the details pane below the list is open.
    /// </summary>
    public bool IsDetailsPaneOpen
    {
        get => (bool)this.GetValue(IsDetailsPaneOpenProperty);
        set => this.SetValue(IsDetailsPaneOpenProperty, value);
    }

    /// <summary>
    ///     Gets or sets the share of the details pane's width given to the message, the properties taking the rest:
    ///     <c>0.6</c> splits it 60/40. The user changes it with the splitter between them; values are kept between
    ///     <see cref="MinDetailsSplitRatio" /> and <see cref="MaxDetailsSplitRatio" />.
    /// </summary>
    public double DetailsSplitRatio
    {
        get => (double)this.GetValue(DetailsSplitRatioProperty);
        set => this.SetValue(DetailsSplitRatioProperty, value);
    }

    /// <summary>Configures whether the console or its host owns scrolling.</summary>
    /// <param name="sender">The console whose setting changed.</param>
    /// <param name="args">The new scrolling setting.</param>
    private static void OnScrollEnabledChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var view = (OutputConsoleView)sender;
        var enabled = (bool)args.NewValue;
        ScrollViewer.SetVerticalScrollMode(view.List, enabled ? ScrollMode.Auto : ScrollMode.Disabled);
        ScrollViewer.SetVerticalScrollBarVisibility(view.List, enabled ? ScrollBarVisibility.Auto : ScrollBarVisibility.Disabled);
        ScrollViewer.SetHorizontalScrollMode(view.List, enabled ? ScrollMode.Auto : ScrollMode.Disabled);
        ScrollViewer.SetHorizontalScrollBarVisibility(view.List, enabled ? ScrollBarVisibility.Auto : ScrollBarVisibility.Disabled);
    }

    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var view = (OutputConsoleView)d;
        view.DetachSource(e.OldValue as IEnumerable);
        if (view.isLoaded)
        {
            view.AttachSource(e.NewValue as IEnumerable);
            view.RebuildView();
        }
    }

    private static void OnToolbarVisibilityChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).ApplyLayerState();

    private static void OnFilterChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnFiltersChanged();

    private static void OnTextFilterChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).ScheduleSearch();

    private static void OnSearchModeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnSearchModeChanged();

    private static void OnViewOptionChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).ApplyViewOptions();

    private static void OnFollowTailChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnFollowTailChanged((bool)e.NewValue);

    private static void OnIsPausedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnIsPausedChanged((bool)e.NewValue);

    private static void OnSelectedItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnSelectedItemChanged(e.NewValue as OutputLogEntry);

    private static void OnDetailsSplitRatioChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).ApplyDetailsSplit();

    private static void OnIsDetailsPaneOpenChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((OutputConsoleView)d).OnIsDetailsPaneOpenChanged((bool)e.NewValue);
}
