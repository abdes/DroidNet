// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>
/// The picker <i>Reassign…</i> opens on a kept change (scene instances 6.4): the source's nodes, those new in the model's
/// last import first, with Find; a node that cannot be chosen says why, and choosing a node whose own changes become kept
/// says how many first.
/// </summary>
public sealed partial class KeptChangeTargetPicker
{
    private readonly IReadOnlyList<KeptChangeTargetItem> all;

    /// <summary>Initializes a new instance of the <see cref="KeptChangeTargetPicker"/> class.</summary>
    /// <param name="choices">What the picker offers.</param>
    public KeptChangeTargetPicker(KeptChangeChoices choices)
    {
        ArgumentNullException.ThrowIfNull(choices);
        this.Title = $"Reassign '{choices.Row.Names}'";
        this.Scope = choices.Scope;
        this.all = [.. choices.Targets.Select(static target => new KeptChangeTargetItem(target))];
        foreach (var item in this.all)
        {
            this.Shown.Add(item);
        }

        this.InitializeComponent();
    }

    /// <summary>Occurs when a node is chosen and confirmed.</summary>
    public event EventHandler<KeptChangeTargetChosenEventArgs>? Chosen;

    /// <summary>Gets the picker's title.</summary>
    public string Title { get; }

    /// <summary>Gets what the repair reaches.</summary>
    public string Scope { get; }

    /// <summary>Gets the nodes Find leaves.</summary>
    public ObservableCollection<KeptChangeTargetItem> Shown { get; } = [];

    private void OnFindChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender;
        _ = args;
        var text = this.FindBox.Text.Trim();
        this.Shown.Clear();
        foreach (var item in this.all.Where(item => text.Length == 0 || item.Names.Contains(text, StringComparison.OrdinalIgnoreCase)))
        {
            this.Shown.Add(item);
        }
    }

    private void OnSelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        _ = sender;
        _ = args;
        var selected = this.TargetList.SelectedItem as KeptChangeTargetItem;
        this.ReassignButton.IsEnabled = selected is { IsEnabled: true };
        var caution = selected switch
        {
            { IsEnabled: false } => selected.Target.Refusal!,
            { Target.Kept: > 0 } => string.Format(
                CultureInfo.CurrentCulture,
                "{0} change(s) this scene makes of '{1}' under its own ID become kept changes.",
                selected.Target.Kept,
                selected.Names),
            _ => string.Empty,
        };
        this.CautionText.Text = caution;
        this.CautionText.Visibility = caution.Length == 0 ? Visibility.Collapsed : Visibility.Visible;
    }

    private void OnReassignClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.TargetList.SelectedItem is KeptChangeTargetItem { IsEnabled: true } item)
        {
            this.Chosen?.Invoke(this, new KeptChangeTargetChosenEventArgs(item.Target.Path));
        }
    }
}
