// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using System.Reactive.Concurrency;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <content>
/// The caution box of the instance's kept changes (scene instances 6.4, 8.10): each change whose node the source no
/// longer holds, with <i>Reassign…</i> and <i>Remove</i>, and <i>Remove all</i>.
/// </content>
public sealed partial class SceneInstanceViewModel
{
    /// <summary>Occurs when the kept changes are asked to come into view, as Go To Property of a cook warning asks.</summary>
    public event EventHandler? KeptChangesFocusRequested;

    /// <summary>Gets the instance's kept changes; empty while its source's structure is not read.</summary>
    public ObservableCollection<KeptChangeRow> KeptChangeRows { get; } = [];

    /// <summary>Gets the caution box's title, such as <c>2 kept changes</c>.</summary>
    [ObservableProperty]
    public partial string KeptChangesTitle { get; private set; } = string.Empty;

    /// <summary>Gets what kept changes are, for the source shown.</summary>
    [ObservableProperty]
    public partial string KeptChangesMessage { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the instance has kept changes.</summary>
    [ObservableProperty]
    public partial bool HasKeptChanges { get; private set; }

    /// <summary>Gets a value indicating whether kept changes can be reassigned and removed now.</summary>
    public bool CanEditKeptChanges => this.CanSwap && this.HasKeptChanges;

    /// <summary>Expands the section and asks its view to bring the kept changes into view.</summary>
    internal void FocusKeptChanges()
    {
        this.IsExpanded = true;
        this.KeptChangesFocusRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Gets what <i>Reassign…</i> offers for a kept change.</summary>
    /// <param name="row">The kept change.</param>
    /// <returns>The choices, or <see langword="null"/> while the source's structure is not read.</returns>
    internal KeptChangeChoices? ChoicesOf(KeptChangeRow row)
    {
        ArgumentNullException.ThrowIfNull(row);
        var project = this.session.Context;
        if (this.instance is not { } shown
            || this.authoringProvider() is not { } authoring
            || this.structures?.Find(project, shown.Source) is not { } structure
            || !KeptChanges.IsComplete(structure))
        {
            return null;
        }

        var record = KeptChanges.RecordOf(project, shown.Source);
        var repair = KeptChanges.RepairOf(shown, row.Change.Path, record);
        var scope = repair == KeptChangeRepair.Model
            ? $"The chosen node continues '{row.Names}' in the import settings of {shown.Source.Name}, which is reimported: the change applies in every scene that places it."
            : "The change moves to the chosen node of this instance only.";
        return new KeptChangeChoices(row, repair, scope, KeptChanges.TargetsOf(authoring.Scene, shown, row.Change, structure, record));
    }

    /// <summary>Removes kept changes of the selected instance.</summary>
    /// <param name="rows">The kept changes, each from <see cref="KeptChangeRows"/>.</param>
    /// <returns>A task that completes once the removal applied or was refused.</returns>
    internal Task RemoveKeptChangesAsync(IReadOnlyList<KeptChangeRow> rows)
    {
        ArgumentNullException.ThrowIfNull(rows);
        if (!this.CanEditKeptChanges || this.root is not { } node || rows.Count == 0)
        {
            return Task.CompletedTask;
        }

        this.LogKeptChangeEditRequested(RemoveKeptChanges.HistoryLabel, [.. rows.Select(static row => row.Names)]);
        this.pending = this.RunKeptChangeEditAsync(new RemoveKeptChanges(node, [.. rows.Select(static row => row.Change.Path)]), "The kept changes could not be removed.");
        return this.pending;
    }

    /// <summary>Reassigns a kept change of the selected instance to a node of its source.</summary>
    /// <param name="row">The kept change.</param>
    /// <param name="target">The chosen node's path below the instance root.</param>
    /// <returns>A task that completes once the reassignment applied or was refused.</returns>
    internal Task ReassignAsync(KeptChangeRow row, NodePath target)
    {
        ArgumentNullException.ThrowIfNull(row);
        ArgumentNullException.ThrowIfNull(target);
        if (!this.CanEditKeptChanges || this.root is not { } node)
        {
            return Task.CompletedTask;
        }

        this.LogKeptChangeEditRequested(ReassignKeptChange.HistoryLabel, [row.Names]);
        this.pending = this.RunKeptChangeEditAsync(new ReassignKeptChange(node, row.Change.Path, target), $"'{row.Names}' could not be reassigned.");
        return this.pending;
    }

    private static string KeptCount(int count)
        => count == 1 ? "1 kept change" : string.Create(CultureInfo.InvariantCulture, $"{count} kept changes");

    private void RefreshKeptChanges(SceneInstanceComponent? shown)
    {
        var kept = this.isMixed || shown is null || this.root is null
            ? []
            : KeptChanges.Of(shown, this.structures?.Find(this.session.Context, shown.Source));
        if (!kept.SequenceEqual(this.KeptChangeRows.Select(static row => row.Change)))
        {
            this.KeptChangeRows.Clear();
            foreach (var change in kept)
            {
                this.KeptChangeRows.Add(new KeptChangeRow(change));
            }
        }

        this.HasKeptChanges = !kept.IsEmpty;
        this.KeptChangesTitle = kept.IsEmpty ? string.Empty : KeptCount(kept.Length);
        this.KeptChangesMessage = kept.IsEmpty || shown is null
            ? string.Empty
            : $"{shown.Source.Name} no longer has the nodes these changes were made on. They have no effect until reassigned to a node, or until the node comes back.";
    }

    private void OnStructuresChanged(object? sender, EventArgs args)
        => _ = this.scheduler.Schedule(() => this.Refresh(this.instance));

    private async Task RunKeptChangeEditAsync(ISceneEdit edit, string failure)
    {
        if (this.authoringProvider() is not { } authoring)
        {
            return;
        }

        this.isBusy = true;
        this.ErrorMessage = string.Empty;
        this.NotifyCommandsChanged();
        try
        {
            var result = await authoring.ExecuteAsync(edit).ConfigureAwait(true);
            if (!result.Succeeded)
            {
                var error = result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError);
                this.LogKeptChangeEditRefused(edit.Label, error?.Code);
                this.ErrorMessage = error?.Message ?? failure;
            }

            if (this.operationResults is { } publisher && this.statusReducer is { } reducer)
            {
                SceneOperationResults.PublishEditResult(publisher, reducer, SceneOperationKinds.EditInstance, $"{edit.Label} refused", "Changes kept", result, new AffectedScope { SceneId = authoring.Scene.Id });
            }
        }
        finally
        {
            this.isBusy = false;
            this.NotifyCommandsChanged();
        }
    }
}
