// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>
/// One script slot in the Scripting section: its script, execution order and position, its problems, and one field per
/// declared parameter.
/// </summary>
public sealed partial class ScriptSlotCardViewModel : ObservableObject
{
    /// <summary>The most problems the badge tooltip lists before summarizing the rest.</summary>
    private const int MaxListedProblems = 5;

    private ScriptRead? read;
    private bool reading;
    private bool updating;
    private bool inOrderSession;

    /// <summary>Initializes a new instance of the <see cref="ScriptSlotCardViewModel"/> class.</summary>
    /// <param name="owner">The Scripting section.</param>
    /// <param name="slot">The slot.</param>
    internal ScriptSlotCardViewModel(ScriptingViewModel owner, ScriptSlot slot)
    {
        this.Owner = owner;
        this.Slot = slot;
    }

    /// <summary>Gets the Scripting section.</summary>
    public ScriptingViewModel Owner { get; }

    /// <summary>Gets the slot.</summary>
    public ScriptSlot Slot { get; private set; }

    /// <summary>Gets the slot identity.</summary>
    public Guid SlotId => this.Slot.Id;

    /// <summary>Gets the scripts the slot can run, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel ScriptChoices => this.Owner.ScriptChoices;

    /// <summary>Gets one field per declared parameter.</summary>
    public ObservableCollection<ScriptParameterFieldViewModel> Fields { get; } = [];

    /// <summary>Gets the values the script no longer declares, or declares with another type.</summary>
    public ObservableCollection<ScriptStaleValueViewModel> StaleValues { get; } = [];

    /// <summary>Gets the card title: the script name, or <c>No script</c>.</summary>
    [ObservableProperty]
    public partial string Title { get; private set; } = "No script";

    /// <summary>Gets the script picker's text: the script name, or <c>Pick a script</c>.</summary>
    [ObservableProperty]
    public partial string ScriptText { get; private set; } = "Pick a script";

    /// <summary>Gets the missing script's path when the slot refers to a script that no longer exists.</summary>
    [ObservableProperty]
    public partial string MissingScriptText { get; private set; } = string.Empty;

    /// <summary>Gets or sets the execution order.</summary>
    [ObservableProperty]
    public partial double ExecutionOrder { get; set; }

    /// <summary>Gets a value indicating whether the slot can move up.</summary>
    [ObservableProperty]
    public partial bool CanMoveUp { get; private set; }

    /// <summary>Gets a value indicating whether the slot can move down.</summary>
    [ObservableProperty]
    public partial bool CanMoveDown { get; private set; }

    /// <summary>Gets the header badge text: the error and warning counts, for example <c>2 errors</c>.</summary>
    [ObservableProperty]
    public partial string BadgeText { get; private set; } = string.Empty;

    /// <summary>Gets the badge tooltip: the problems, one per line.</summary>
    [ObservableProperty]
    public partial string BadgeToolTip { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the badge reports errors rather than warnings.</summary>
    [ObservableProperty]
    public partial bool BadgeIsError { get; private set; }

    /// <summary>Gets the explanation shown above the fields: out-of-date declarations, no parameters, or an
    /// unanalyzed script.</summary>
    [ObservableProperty]
    public partial string Notice { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the shown declarations come from an earlier, valid version of the
    /// script.</summary>
    [ObservableProperty]
    public partial bool ParametersOutOfDate { get; private set; }

    /// <summary>Gets a value indicating whether the slot has a badge.</summary>
    public bool HasBadge => this.BadgeText.Length > 0;

    /// <summary>Gets a value indicating whether the slot shows a notice.</summary>
    public bool HasNotice => this.Notice.Length > 0;

    /// <summary>Gets a value indicating whether the slot refers to a missing script.</summary>
    public bool IsScriptMissing => this.MissingScriptText.Length > 0;

    /// <summary>Gets a value indicating whether the slot holds stale values.</summary>
    public bool HasStaleValues => this.StaleValues.Count > 0;

    /// <summary>Gets a value indicating whether there are enough stale values to offer removing them all at once.</summary>
    public bool CanRemoveAllStale => this.StaleValues.Count > 1;

    /// <summary>Gets a value indicating whether the script's source can be opened in the code editor.</summary>
    public bool CanEditScript => this.Script?.SourcePath is not null;

    /// <summary>Gets the Edit button tooltip, which says why Edit is unavailable when it is.</summary>
    public string EditToolTip => this.Script?.Origin switch
    {
        ScriptOrigin.Source => "Edit in your code editor",
        ScriptOrigin.Library => "This script comes from a cooked library; it has no source in the project.",
        _ when this.reading || this.read?.Failure is not null => "This script comes from a cooked library; it has no source in the project.",
        _ => this.Slot.Script is null ? "Pick a script to edit it." : "The script's source is not known yet.",
    };

    /// <summary>Gets a value indicating whether the slot can be edited.</summary>
    public bool CanEdit => this.Owner.CanEdit;

    /// <summary>Gets the slot's script, when it is known.</summary>
    private Script? Script => this.read?.Script;

    /// <summary>Starts an interactive execution order edit; it is committed when it completes.</summary>
    public void BeginOrderSession() => this.inOrderSession = true;

    /// <summary>Completes an interactive execution order edit.</summary>
    /// <param name="commit">Whether to commit the edited order.</param>
    public void CompleteOrderSession(bool commit)
    {
        this.inOrderSession = false;
        if (commit)
        {
            this.CommitOrder();
        }
        else
        {
            this.ShowOrder();
        }
    }

    /// <summary>Sets one parameter value, or resets it when the value is null.</summary>
    /// <param name="parameter">The declared parameter.</param>
    /// <param name="value">The value.</param>
    /// <returns>The completion of the undoable edit.</returns>
    internal Task SetValueAsync(ScriptParameter parameter, ScriptValue? value)
        => this.Owner.EditAsync(node => new SetScriptValue(node, new ScriptSlotId(this.SlotId), parameter, value));

    /// <summary>Makes the slot run another script; picking its current script changes nothing.</summary>
    /// <param name="script">The picked script.</param>
    /// <returns>The completion of the undoable edit.</returns>
    internal Task SetScriptAsync(AssetPath script)
        => script == this.Slot.Script
            ? Task.CompletedTask
            : this.Owner.EditAsync(node => new SetSlotScript(node, new ScriptSlotId(this.SlotId), script));

    /// <summary>Removes one value, so the parameter takes its declared default or the stale value is gone.</summary>
    /// <param name="name">The parameter name.</param>
    /// <returns>The completion of the undoable edit.</returns>
    internal Task RemoveValueAsync(string name)
        => this.Owner.EditAsync(node => new ResetScriptValue(node, new ScriptSlotId(this.SlotId), name));

    /// <summary>Shows the slot, its position and its script.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="index">The slot position.</param>
    /// <param name="count">The number of slots.</param>
    /// <param name="script">The slot's script, from its analysis or a library read; <see langword="null"/> when it is
    /// not known.</param>
    /// <param name="isReading">Whether the script is being read from a library.</param>
    internal void Update(ScriptSlot slot, int index, int count, ScriptRead? script, bool isReading)
    {
        this.Slot = slot;
        this.read = script;
        this.reading = isReading;
        this.CanMoveUp = index > 0;
        this.CanMoveDown = index < count - 1;
        var picked = slot.Script is { } identity ? this.ScriptChoices.Find(identity) : null;
        this.Title = picked?.Name ?? (slot.Script is { } uri ? Path.GetFileNameWithoutExtension(uri.VirtualPath) : "No script");
        this.ScriptText = slot.Script is null ? "Pick a script" : this.Title;
        this.MissingScriptText = slot.Script is { } missing && picked is null && !isReading && script?.Script is null && script?.Failure is null
            ? "Missing: " + missing.VirtualPath
            : string.Empty;
        if (!this.inOrderSession)
        {
            this.ShowOrder();
        }

        this.UpdateBadge(this.Script);
        this.UpdateFields(slot);
        this.OnPropertyChanged(nameof(this.CanEdit));
        this.OnPropertyChanged(nameof(this.CanEditScript));
        this.OnPropertyChanged(nameof(this.EditToolTip));
    }

    /// <summary>Refreshes edit availability after the section's state changed.</summary>
    internal void RefreshCanEdit()
    {
        this.OnPropertyChanged(nameof(this.CanEdit));
        foreach (var field in this.Fields)
        {
            field.Update(field.Parameter, this.Slot.Overrides.GetValueOrDefault(field.Name));
        }

        foreach (var stale in this.StaleValues)
        {
            stale.RefreshCanEdit();
        }
    }

    private static string DescribeProblem(ScriptProblem problem)
    {
        var severity = problem.Severity >= DiagnosticSeverity.Error ? "Error" : "Warning";
        var line = problem.Line is { } number ? string.Create(CultureInfo.CurrentCulture, $", line {number}") : string.Empty;
        return string.Create(CultureInfo.CurrentCulture, $"{severity} {problem.Code}{line}: {problem.Message}");
    }

    partial void OnExecutionOrderChanged(double value)
    {
        _ = value;
        if (!this.updating && !this.inOrderSession)
        {
            this.CommitOrder();
        }
    }

    partial void OnBadgeTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasBadge));
    }

    partial void OnNoticeChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasNotice));
    }

    partial void OnMissingScriptTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.IsScriptMissing));
    }

    [RelayCommand]
    private Task RemoveAsync() => this.Owner.EditAsync(node => new RemoveScriptSlot(node, new ScriptSlotId(this.SlotId)));

    [RelayCommand]
    private Task MoveUpAsync() => this.Owner.EditAsync(node => new MoveScriptSlot(node, new ScriptSlotId(this.SlotId), -1));

    [RelayCommand]
    private Task MoveDownAsync() => this.Owner.EditAsync(node => new MoveScriptSlot(node, new ScriptSlotId(this.SlotId), 1));

    [RelayCommand]
    private Task NewScriptAsync() => this.Owner.CreateScriptForAsync(this.SlotId);

    [RelayCommand]
    private Task EditScriptAsync() => this.Owner.EditScriptAsync(this.Script?.SourcePath, line: null);

    [RelayCommand]
    private void OpenProblem()
    {
        var problems = this.Script?.Problems ?? [];
        if ((problems.FirstOrDefault(static problem => problem.Severity >= DiagnosticSeverity.Error)
            ?? problems.FirstOrDefault(static problem => problem.Severity == DiagnosticSeverity.Warning)) is { } problem)
        {
            this.Owner.ShowProblem(problem);
        }
    }

    [RelayCommand]
    private Task RemoveStaleAsync()
        => this.Script is { } script
            ? this.Owner.EditAsync(node => new RemoveStaleScriptValues(node, new ScriptSlotId(this.SlotId), script.Parameters))
            : Task.CompletedTask;

    private void CommitOrder()
    {
        var order = (int)Math.Round(this.ExecutionOrder);
        if (order != this.Slot.ExecutionOrder)
        {
            _ = this.Owner.EditAsync(node => new SetSlotExecutionOrder(node, new ScriptSlotId(this.SlotId), order));
        }
    }

    private void ShowOrder()
    {
        this.updating = true;
        try
        {
            this.ExecutionOrder = this.Slot.ExecutionOrder;
        }
        finally
        {
            this.updating = false;
        }
    }

    // The badge shows the counts; its tooltip lists the errors, then the warnings.
    private void UpdateBadge(Script? script)
    {
        var problems = (script?.Problems ?? [])
            .Where(static problem => problem.Severity >= DiagnosticSeverity.Warning)
            .OrderBy(static problem => problem.Severity >= DiagnosticSeverity.Error ? 0 : 1)
            .ToArray();
        this.BadgeIsError = script?.ErrorCount > 0;
        this.BadgeText = script is null || problems.Length == 0 ? string.Empty : script.ProblemSummary;
        var lines = problems.Take(MaxListedProblems).Select(DescribeProblem);
        var more = problems.Length > MaxListedProblems
            ? [string.Create(CultureInfo.CurrentCulture, $"and {problems.Length - MaxListedProblems} more")]
            : Array.Empty<string>();
        this.BadgeToolTip = problems.Length == 0 ? string.Empty
            : string.Join(System.Environment.NewLine, lines.Concat(more).Append("Click to open Problems."));
    }

    private void UpdateFields(ScriptSlot slot)
    {
        var declared = this.Script?.Parameters ?? [];
        this.ParametersOutOfDate = this.Script?.ParametersOutOfDate == true;
        this.Notice = slot.Script is null ? "Pick a script to set its parameters." : this.DescribeScript();

        // Fields are kept across updates, so a field that has focus keeps it when an unrelated value changes.
        for (var index = this.Fields.Count - 1; index >= 0; index--)
        {
            var field = this.Fields[index];
            if (declared.FirstOrDefault(parameter => string.Equals(parameter.Name, field.Name, StringComparison.Ordinal)) is not { } parameter
                || parameter.Kind != field.Parameter.Kind || parameter.IsColor != field.Parameter.IsColor
                || (parameter.Options.Count > 0) != (field.Parameter.Options.Count > 0))
            {
                this.Fields.RemoveAt(index);
            }
        }

        for (var index = 0; index < declared.Count; index++)
        {
            var parameter = declared[index];
            var field = this.Fields.FirstOrDefault(candidate => string.Equals(candidate.Name, parameter.Name, StringComparison.Ordinal));
            if (field is null)
            {
                field = new ScriptParameterFieldViewModel(this, parameter);
                this.Fields.Insert(Math.Min(index, this.Fields.Count), field);
            }
            else if (this.Fields.IndexOf(field) != index)
            {
                this.Fields.Move(this.Fields.IndexOf(field), index);
            }

            var value = slot.Overrides.GetValueOrDefault(parameter.Name);
            field.Update(parameter, value is not null && value.Kind == parameter.ValueKind ? value : null);
        }

        this.StaleValues.Clear();
        if (this.Script is not null && !this.ParametersOutOfDate)
        {
            foreach (var name in ScriptSlotRules.FindStale(slot, declared))
            {
                this.StaleValues.Add(new ScriptStaleValueViewModel(this, name, slot.Overrides[name]));
            }
        }

        this.OnPropertyChanged(nameof(this.HasStaleValues));
        this.OnPropertyChanged(nameof(this.CanRemoveAllStale));
    }

    private string DescribeScript()
        => this.Script switch
        {
            null when this.reading => "Reading the parameters of this library script.",
            null when this.read?.Failure is { } failure => "The parameters of this library script could not be read: " + failure,
            null => "The script has not been analyzed yet, so its parameters are unknown.",
            _ when this.ParametersOutOfDate => "The script has errors. These parameters are from its last valid version; fix the errors to see the current ones.",
            { Parameters.Count: 0 } => "This script declares no parameters.",
            { Origin: ScriptOrigin.Library } => "This script comes from a cooked library, which keeps no ranges, units or descriptions.",
            _ => string.Empty,
        };
}
