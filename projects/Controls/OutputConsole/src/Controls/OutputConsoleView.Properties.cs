// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Partial declarations for <see cref="OutputConsoleView" /> related to
///     dependency properties and related public properties.
/// </summary>
public partial class OutputConsoleView
{
    /// <summary>
    ///     Identifies the <see cref="TextFilter" /> dependency property.
    ///     Holds the substring used to filter displayed log messages.
    /// </summary>
    public static readonly DependencyProperty TextFilterProperty = DependencyProperty.Register(
        nameof(TextFilter),
        typeof(string),
        typeof(OutputConsoleView),
        new PropertyMetadata(string.Empty, OnFilterChanged));

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
    ///     Identifies the <see cref="SelectedItem" /> dependency property.
    ///     Represents the currently selected <see cref="OutputLogEntry" /> in the view.
    /// </summary>
    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem),
        typeof(OutputLogEntry),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: null));

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
        new PropertyMetadata(defaultValue: true));

    /// <summary>
    ///     Identifies the <see cref="IsPaused" /> dependency property.
    ///     When true, live updates are suspended.
    /// </summary>
    public static readonly DependencyProperty IsPausedProperty = DependencyProperty.Register(
        nameof(IsPaused),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false));

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
    ///     Identifies the <see cref="WordWrap" /> dependency property.
    ///     Controls whether message text wraps in the view.
    /// </summary>
    public static readonly DependencyProperty WordWrapProperty = DependencyProperty.Register(
        nameof(WordWrap),
        typeof(bool),
        typeof(OutputConsoleView),
        new PropertyMetadata(defaultValue: false, OnViewOptionChanged));

    /// <summary>
    ///     Gets or sets the substring used to filter displayed log messages.
    ///     Bound to <see cref="TextFilterProperty" />.
    /// </summary>
    public string TextFilter
    {
        get => (string)this.GetValue(TextFilterProperty);
        set => this.SetValue(TextFilterProperty, value);
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
    ///     Gets or sets the currently selected <see cref="OutputLogEntry" />.
    ///     Bound to <see cref="SelectedItemProperty" />.
    /// </summary>
    public OutputLogEntry? SelectedItem
    {
        get => (OutputLogEntry?)this.GetValue(SelectedItemProperty);
        set => this.SetValue(SelectedItemProperty, value);
    }

    /// <summary>
    ///     Gets or sets the collection that serves as the control's source of log entries.
    ///     Bound to <see cref="ItemsSourceProperty" />.
    /// </summary>
    public IEnumerable? ItemsSource
    {
        get => (IEnumerable?)this.GetValue(ItemsSourceProperty);
        set => this.SetValue(ItemsSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the view will auto-scroll to the newest entries.
    ///     Bound to <see cref="FollowTailProperty" />.
    /// </summary>
    public bool FollowTail
    {
        get => (bool)this.GetValue(FollowTailProperty);
        set => this.SetValue(FollowTailProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether incoming log updates are paused.
    ///     Bound to <see cref="IsPausedProperty" />.
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
    ///     Gets or sets a value indicating whether message text is wrapped in the UI.
    ///     Bound to <see cref="WordWrapProperty" />.
    /// </summary>
    public bool WordWrap
    {
        get => (bool)this.GetValue(WordWrapProperty);
        set => this.SetValue(WordWrapProperty, value);
    }

    /// <summary>
    ///     Called when the <see cref="ItemsSource" /> property changes.
    ///     Detaches from the old collection, attaches to the new one and rebuilds the view.
    /// </summary>
    /// <param name="d">The dependency object that changed (expected <see cref="OutputConsoleView" />).</param>
    /// <param name="e">Provides old and new values for the property.</param>
    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.DetachCollectionChanged(e.OldValue as IEnumerable);

        // Keep ListView bound to proxy; just rebuild its content from the new source
        v.AttachCollectionChanged(e.NewValue as IEnumerable);
        v.RebuildView();
    }

    /// <summary>
    ///     Called when either the <see cref="TextFilter" /> or <see cref="LevelFilter" /> changes.
    ///     Requests the control to refresh its filter.
    /// </summary>
    /// <param name="d">The dependency object that changed (expected <see cref="OutputConsoleView" />).</param>
    /// <param name="e">Provides old and new values for the property.</param>
    private static void OnFilterChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.RefreshFilter();
    }

    /// <summary>
    ///     Called when view-level display options (like <see cref="ShowTimestamps" /> or <see cref="WordWrap" />)
    ///     change and the UI needs to update.
    /// </summary>
    /// <param name="d">The dependency object that changed (expected <see cref="OutputConsoleView" />).</param>
    /// <param name="e">Provides old and new values for the property.</param>
    private static void OnViewOptionChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var v = (OutputConsoleView)d;
        v.ApplyViewOptions();
    }
}
