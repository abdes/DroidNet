// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// The Level of detail editor's rows: a geometry's LOD policy, its kind and its boundaries, one per transition between two
/// of the geometry's LODs.
/// </summary>
/// <remarks>
/// The policy is one value: editing any of its fields writes the whole policy, starting from the value shown, through
/// the owner's write, which records it as one undo step and shows the result with <see cref="Show"/>; reset writes
/// <see langword="null"/>, the inherited policy. A value the owner refuses, such as boundaries out of order, is reported
/// with <see cref="ShowError"/> and never reordered. The owner decides what the policy overrides and says it in the
/// summary and the reset's tooltip, such as a scene node's override of its asset, or a model's import default.
/// </remarks>
public sealed partial class LodPolicyEditorModel : ObservableObject
{
    private const string EditLabel = "Edit Level of Detail";
    private const string ResetLabel = "Reset Level of Detail";

    private readonly Func<LodPolicy?, string, Task> write;
    private LodPolicy inherited = new LodPolicy.Fixed(0);
    private LodPolicy? shown;
    private int lodCount = 1;
    private int kind = -1;

    /// <summary>Initializes a new instance of the <see cref="LodPolicyEditorModel"/> class.</summary>
    /// <param name="write">Writes the whole policy, or <see langword="null"/> to reset it, with its history label.</param>
    public LodPolicyEditorModel(Func<LodPolicy?, string, Task> write)
    {
        ArgumentNullException.ThrowIfNull(write);
        this.write = write;
    }

    /// <summary>Gets the commands the policy row offers besides reset, if any.</summary>
    public IOverrideActionSource? Actions { get; init; }

    /// <summary>Gets the names of the policy kinds, as the kind list offers them.</summary>
    public IReadOnlyList<string> Kinds { get; } = LodPolicyShapes.KindNames;

    /// <summary>Gets a value indicating whether the rows show.</summary>
    [ObservableProperty]
    public partial bool IsAvailable { get; private set; }

    /// <summary>Gets a value indicating whether the policy can be edited now.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>Gets whether the policy is set where it is edited, rather than inherited.</summary>
    [ObservableProperty]
    public partial OverrideState OverrideState { get; private set; }

    /// <summary>Gets the reset's tooltip, which names the inherited policy.</summary>
    [ObservableProperty]
    public partial string ResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets the boundaries disclosure's summary, which names the policy's source.</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether the boundaries show.</summary>
    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    /// <summary>Gets a value indicating whether a Fixed policy shows, with its LOD.</summary>
    [ObservableProperty]
    public partial bool IsFixed { get; private set; }

    /// <summary>Gets a value indicating whether a Distance policy shows, with its thresholds.</summary>
    [ObservableProperty]
    public partial bool IsDistance { get; private set; }

    /// <summary>Gets a value indicating whether a Screen-space error policy shows, with its errors.</summary>
    [ObservableProperty]
    public partial bool IsScreenSpaceError { get; private set; }

    /// <summary>Gets the range of the Fixed LOD, from the geometry's LOD count.</summary>
    [ObservableProperty]
    public partial string FixedRange { get; private set; } = string.Empty;

    /// <summary>Gets the boundaries of the policy shown, one per transition between two LODs.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<LodTransitionModel> Transitions { get; private set; } = [];

    /// <summary>Gets why the last edit was refused, or nothing.</summary>
    [ObservableProperty]
    public partial string ErrorText { get; private set; } = string.Empty;

    /// <summary>Gets the diagnostic code of the last refused edit, or nothing.</summary>
    public string ErrorCode { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the last edit was refused.</summary>
    public bool HasError => this.ErrorText.Length > 0;

    /// <summary>Gets or sets the policy kind, an index in <see cref="Kinds"/>; -1 when the edited policies differ.</summary>
    /// <remarks>
    /// Choosing a kind writes the inherited policy when it is of that kind, else that kind's default boundaries (see
    /// <see cref="LodPolicyShapes"/>); from mixed policies, any kind does.
    /// </remarks>
    public int Kind
    {
        get => this.kind;
        set
        {
            if (value >= 0 && value < this.Kinds.Count && value != this.kind)
            {
                this.PendingEdit = this.write(LodPolicyShapes.Create(value, this.inherited, this.lodCount), EditLabel);
            }
        }
    }

    /// <summary>Gets or sets the LOD a Fixed policy shows, from 0 to the geometry's last LOD.</summary>
    public double FixedLod
    {
        get => this.shown is LodPolicy.Fixed policy ? policy.LodIndex : 0;
        set
        {
            if (this.shown is not LodPolicy.Fixed policy || value == policy.LodIndex)
            {
                return;
            }

            if (value != Math.Floor(value) || value < 0 || value >= this.lodCount)
            {
                this.ShowError(code: string.Empty, string.Create(CultureInfo.CurrentCulture, $"The LOD must be a whole number from 0 to {this.lodCount - 1}."));
                this.OnPropertyChanged(nameof(this.FixedLod));
                return;
            }

            this.Write(new LodPolicy.Fixed((int)value));
        }
    }

    /// <summary>Gets or sets the hysteresis ratio of a Distance policy.</summary>
    public double Hysteresis
    {
        get => this.shown is LodPolicy.Distance policy ? policy.HysteresisRatio : 0;
        set
        {
            if (this.shown is LodPolicy.Distance policy)
            {
                this.Write(policy with { HysteresisRatio = (float)value });
            }
        }
    }

    /// <summary>Gets the completion of the last edit a field started, after which the rows show its result.</summary>
    public Task PendingEdit { get; private set; } = Task.CompletedTask;

    /// <summary>Shows the policy being edited.</summary>
    /// <param name="state">The policy, what it inherits and how the owner presents it.</param>
    public void Show(LodPolicyEditorState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        this.inherited = state.Inherited;
        this.lodCount = state.LodCount;
        this.shown = state.Policy is { } policy ? LodPolicyShapes.Fit(policy, this.lodCount) : null;
        this.kind = this.shown is null ? -1 : LodPolicyShapes.KindOf(this.shown);

        this.IsAvailable = state.IsAvailable;
        this.CanEdit = state.CanEdit;
        this.IsFixed = this.shown is LodPolicy.Fixed;
        this.IsDistance = this.shown is LodPolicy.Distance;
        this.IsScreenSpaceError = this.shown is LodPolicy.ScreenSpaceError;
        this.FixedRange = string.Create(CultureInfo.CurrentCulture, $"of 0 to {this.lodCount - 1}");
        this.OverrideState = state.OverrideState;
        this.ResetToolTip = state.ResetToolTip;
        this.Summary = state.Summary;
        this.UpdateTransitions();

        // The fields show the policy's values even when an edit was refused after they changed.
        this.OnPropertyChanged(nameof(this.Kind));
        this.OnPropertyChanged(nameof(this.FixedLod));
        this.OnPropertyChanged(nameof(this.Hysteresis));
    }

    /// <summary>Hides the rows: there is no policy to edit.</summary>
    public void Clear()
    {
        this.shown = null;
        this.kind = -1;
        this.IsAvailable = false;
        this.Transitions = [];
        this.ClearError();
    }

    /// <summary>Shows why an edit was refused.</summary>
    /// <param name="code">The refusal's diagnostic code, or nothing when the refusal is the editor's own.</param>
    /// <param name="message">What the user reads.</param>
    public void ShowError(string code, string message)
    {
        this.ErrorCode = code;
        this.ErrorText = message;
    }

    /// <summary>Clears the refusal shown.</summary>
    public void ClearError() => this.ShowError(code: string.Empty, message: string.Empty);

    /// <summary>Writes the policy with one Distance threshold changed.</summary>
    /// <param name="index">The boundary's index.</param>
    /// <param name="value">The threshold.</param>
    internal void WriteDistance(int index, double value)
    {
        if (this.shown is LodPolicy.Distance policy)
        {
            this.Write(policy with { Thresholds = policy.Thresholds.SetItem(index, (float)value) });
        }
    }

    /// <summary>Writes the policy with one error changed: at or above it, the finer LOD of the boundary is entered.</summary>
    /// <param name="index">The boundary's index.</param>
    /// <param name="value">The error.</param>
    internal void WriteFiner(int index, double value)
    {
        if (this.shown is LodPolicy.ScreenSpaceError policy)
        {
            this.Write(new LodPolicy.ScreenSpaceError(policy.EnterFinerSse.SetItem(index, (float)value), policy.ExitCoarserSse));
        }
    }

    /// <summary>Writes the policy with one error changed: at or below it, the coarser LOD of the boundary is entered.</summary>
    /// <param name="index">The boundary's index.</param>
    /// <param name="value">The error.</param>
    internal void WriteCoarser(int index, double value)
    {
        if (this.shown is LodPolicy.ScreenSpaceError policy)
        {
            this.Write(new LodPolicy.ScreenSpaceError(policy.EnterFinerSse, policy.ExitCoarserSse.SetItem(index, (float)value)));
        }
    }

    private void Write(LodPolicy policy)
    {
        if (!policy.Equals(this.shown))
        {
            this.PendingEdit = this.write(policy, EditLabel);
        }
    }

    // Keeps the rows, and their focus, while the number of transitions stays the same.
    private void UpdateTransitions()
    {
        var count = this.shown switch
        {
            LodPolicy.Distance distance => distance.Thresholds.Length,
            LodPolicy.ScreenSpaceError error => error.EnterFinerSse.Length,
            _ => 0,
        };
        if (this.Transitions.Count != count)
        {
            this.Transitions = [.. Enumerable.Range(0, count).Select(index => new LodTransitionModel(this, index))];
        }

        foreach (var transition in this.Transitions)
        {
            switch (this.shown)
            {
                case LodPolicy.Distance distance:
                    transition.Show(distance.Thresholds[transition.Index], 0, this.CanEdit);
                    break;
                case LodPolicy.ScreenSpaceError error:
                    transition.Show(error.EnterFinerSse[transition.Index], error.ExitCoarserSse[transition.Index], this.CanEdit);
                    break;
            }
        }
    }

    partial void OnErrorTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasError));
    }

    [RelayCommand]
    private Task ResetAsync() => this.PendingEdit = this.write(null, ResetLabel);
}
