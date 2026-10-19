// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>
/// One script slot in the Scripting section: its script, execution order and position, its problems, and one field per
/// declared parameter.
/// </summary>
public sealed partial class ScriptSlotCardViewModel : ObservableObject
{
    /// <summary>The most problems the badge tooltip lists before summarizing the rest.</summary>
    private const int MaxListedProblems = 5;

    private ScriptDeclarations declarations = ScriptDeclarations.Unknown;
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
    public bool CanEditScript => this.declarations.Analysis is not null;

    /// <summary>Gets the Edit button tooltip, which says why Edit is unavailable when it is.</summary>
    public string EditToolTip => this.declarations.Origin switch
    {
        ScriptDeclarationOrigin.Source => "Edit in your code editor",
        ScriptDeclarationOrigin.Library or ScriptDeclarationOrigin.LibraryLoading or ScriptDeclarationOrigin.LibraryUnavailable
            => "This script comes from a cooked library; it has no source in the project.",
        _ => this.Slot.Script is null ? "Pick a script to edit it." : "The script's source is not known yet.",
    };

    /// <summary>Gets a value indicating whether the slot can be edited.</summary>
    public bool CanEdit => this.Owner.CanEdit;

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
        => this.Owner.EditAsync(slots => ScriptSlotEdits.SetValue(slots, this.SlotId, parameter, value));

    /// <summary>Makes the slot run another script; picking its current script changes nothing.</summary>
    /// <param name="script">The picked script.</param>
    /// <returns>The completion of the undoable edit.</returns>
    internal Task SetScriptAsync(AssetPath script)
        => script == this.Slot.Script
            ? Task.CompletedTask
            : this.Owner.EditAsync(slots => ScriptSlotEdits.SetScript(slots, this.SlotId, script));

    /// <summary>Removes one value, so the parameter takes its declared default or the stale value is gone.</summary>
    /// <param name="name">The parameter name.</param>
    /// <returns>The completion of the undoable edit.</returns>
    internal Task RemoveValueAsync(string name)
        => this.Owner.EditAsync(slots => ScriptSlotEdits.Reset(slots, this.SlotId, name));

    /// <summary>Shows the slot, its position and its script's declarations.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="index">The slot position.</param>
    /// <param name="count">The number of slots.</param>
    /// <param name="declared">The script's declarations and where they come from.</param>
    internal void Update(ScriptSlot slot, int index, int count, ScriptDeclarations declared)
    {
        this.Slot = slot;
        this.declarations = declared;
        this.CanMoveUp = index > 0;
        this.CanMoveDown = index < count - 1;
        var picked = slot.Script is { } script ? this.ScriptChoices.Find(script) : null;
        this.Title = picked?.Name ?? (slot.Script is { } uri ? Path.GetFileNameWithoutExtension(uri.VirtualPath) : "No script");
        this.ScriptText = slot.Script is null ? "Pick a script" : this.Title;
        this.MissingScriptText = slot.Script is { } missing && picked is null && declared.Origin == ScriptDeclarationOrigin.Unknown
            ? "Missing: " + missing.VirtualPath
            : string.Empty;
        if (!this.inOrderSession)
        {
            this.ShowOrder();
        }

        this.UpdateBadge(declared.Analysis);
        this.UpdateFields(slot, declared);
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
        var severity = problem.Severity == ScriptProblemSeverity.Error ? "Error" : "Warning";
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
    private Task RemoveAsync() => this.Owner.EditAsync(slots => ScriptSlotEdits.Remove(slots, this.SlotId));

    [RelayCommand]
    private Task MoveUpAsync() => this.Owner.EditAsync(slots => ScriptSlotEdits.Move(slots, this.SlotId, -1));

    [RelayCommand]
    private Task MoveDownAsync() => this.Owner.EditAsync(slots => ScriptSlotEdits.Move(slots, this.SlotId, 1));

    [RelayCommand]
    private Task NewScriptAsync() => this.Owner.CreateScriptForAsync(this.SlotId);

    [RelayCommand]
    private Task EditScriptAsync() => this.Owner.EditScriptAsync(this.declarations.Analysis?.SourcePath, line: null);

    [RelayCommand]
    private void OpenProblem()
    {
        var problems = this.declarations.Analysis?.Problems ?? [];
        if ((problems.FirstOrDefault(static problem => problem.Severity == ScriptProblemSeverity.Error)
            ?? problems.FirstOrDefault(static problem => problem.Severity == ScriptProblemSeverity.Warning)) is { } problem)
        {
            this.Owner.ShowProblem(problem);
        }
    }

    [RelayCommand]
    private Task RemoveStaleAsync()
        => this.declarations.AreKnown
            ? this.Owner.EditAsync(slots => ScriptSlotEdits.RemoveStale(slots, this.SlotId, this.declarations.Parameters))
            : Task.CompletedTask;

    private void CommitOrder()
    {
        var order = (int)Math.Round(this.ExecutionOrder);
        if (order != this.Slot.ExecutionOrder)
        {
            _ = this.Owner.EditAsync(slots => ScriptSlotEdits.SetExecutionOrder(slots, this.SlotId, order));
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
    private void UpdateBadge(ScriptAnalysisSnapshot? snapshot)
    {
        var problems = (snapshot?.Problems ?? [])
            .Where(static problem => problem.Severity is ScriptProblemSeverity.Error or ScriptProblemSeverity.Warning)
            .OrderBy(static problem => problem.Severity == ScriptProblemSeverity.Error ? 0 : 1)
            .ToArray();
        this.BadgeIsError = snapshot?.ErrorCount > 0;
        this.BadgeText = snapshot is null || problems.Length == 0 ? string.Empty : snapshot.ProblemSummary;
        var lines = problems.Take(MaxListedProblems).Select(DescribeProblem);
        var more = problems.Length > MaxListedProblems
            ? [string.Create(CultureInfo.CurrentCulture, $"and {problems.Length - MaxListedProblems} more")]
            : Array.Empty<string>();
        this.BadgeToolTip = problems.Length == 0 ? string.Empty
            : string.Join(System.Environment.NewLine, lines.Concat(more).Append("Click to open Problems."));
    }

    private void UpdateFields(ScriptSlot slot, ScriptDeclarations declarations)
    {
        var declared = declarations.Parameters;
        this.ParametersOutOfDate = declarations.Analysis?.ParametersOutOfDate == true;
        this.Notice = slot.Script is null ? "Pick a script to set its parameters." : this.DescribeDeclarations(declarations);

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
            field.Update(parameter, value is not null && value.Kind == ScriptSlotEdits.ToValueKind(parameter.Kind) ? value : null);
        }

        this.StaleValues.Clear();
        if (declarations.AreKnown && !this.ParametersOutOfDate)
        {
            foreach (var name in ScriptSlotEdits.FindStale(slot, declared))
            {
                this.StaleValues.Add(new ScriptStaleValueViewModel(this, name, slot.Overrides[name]));
            }
        }

        this.OnPropertyChanged(nameof(this.HasStaleValues));
        this.OnPropertyChanged(nameof(this.CanRemoveAllStale));
    }

    private string DescribeDeclarations(ScriptDeclarations declarations)
        => declarations.Origin switch
        {
            ScriptDeclarationOrigin.Unknown => "The script has not been analyzed yet, so its parameters are unknown.",
            ScriptDeclarationOrigin.LibraryLoading => "Reading the parameters of this library script.",
            ScriptDeclarationOrigin.LibraryUnavailable => "The parameters of this library script could not be read: " + declarations.Failure,
            _ when this.ParametersOutOfDate => "The script has errors. These parameters are from its last valid version; fix the errors to see the current ones.",
            _ when declarations.Parameters.Count == 0 => "This script declares no parameters.",
            ScriptDeclarationOrigin.Library => "This script comes from a cooked library, which keeps no ranges, units or descriptions.",
            _ => string.Empty,
        };
}
