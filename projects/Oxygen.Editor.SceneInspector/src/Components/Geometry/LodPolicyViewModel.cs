// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
/// The Level of detail rows of the Geometry section: the selected nodes' LOD policy, their override or the asset's, and its
/// boundaries, one per transition between two of the geometry's LODs.
/// </summary>
/// <remarks>
/// The policy is one property: editing any of its fields writes the whole policy as the nodes' override, starting from the
/// value shown, in one undo step; reset removes the override so the asset's policy applies. A value the scene refuses, such
/// as boundaries out of order, is reported with its diagnostic and never reordered. <see cref="GeometryViewModel"/> runs
/// the edits and updates the rows from the scene and the inventory after each one.
/// </remarks>
public sealed partial class LodPolicyViewModel : ObservableObject
{
    private readonly GeometryViewModel owner;
    private LodPolicy asset = SceneSchema.Geometry.Defaults.LodPolicy;
    private LodPolicy? shown;
    private int lodCount = 1;
    private int kind = -1;

    /// <summary>Initializes a new instance of the <see cref="LodPolicyViewModel"/> class.</summary>
    /// <param name="owner">The geometry section that edits the policy.</param>
    internal LodPolicyViewModel(GeometryViewModel owner) => this.owner = owner;

    /// <summary>Gets the names of the policy kinds, as the kind list offers them.</summary>
    public IReadOnlyList<string> Kinds => LodPolicyShapes.KindNames;

    /// <summary>
    /// Gets a value indicating whether the rows show: the selected nodes share one geometry whose inventory is read, and
    /// it has more than one LOD or the nodes override its policy.
    /// </summary>
    [ObservableProperty]
    public partial bool IsAvailable { get; private set; }

    /// <summary>Gets a value indicating whether the policy can be edited now.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>Gets how many selected nodes override the asset's policy.</summary>
    [ObservableProperty]
    public partial OverrideState OverrideState { get; private set; }

    /// <summary>Gets the reset's tooltip, which names the asset's policy.</summary>
    [ObservableProperty]
    public partial string ResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets the boundaries disclosure's summary, which names the policy's source.</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether the boundaries show.</summary>
    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    /// <summary>Gets a value indicating whether the nodes agree on a Fixed policy, whose LOD shows.</summary>
    [ObservableProperty]
    public partial bool IsFixed { get; private set; }

    /// <summary>Gets a value indicating whether the nodes agree on a Distance policy, whose thresholds show.</summary>
    [ObservableProperty]
    public partial bool IsDistance { get; private set; }

    /// <summary>Gets a value indicating whether the nodes agree on a Screen-space error policy, whose errors show.</summary>
    [ObservableProperty]
    public partial bool IsScreenSpaceError { get; private set; }

    /// <summary>Gets the range of the Fixed LOD, from the geometry's LOD count.</summary>
    [ObservableProperty]
    public partial string FixedRange { get; private set; } = string.Empty;

    /// <summary>Gets the boundaries of the policy shown, one per transition between two LODs.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<LodTransitionViewModel> Transitions { get; private set; } = [];

    /// <summary>Gets why the last edit was refused, or nothing.</summary>
    [ObservableProperty]
    public partial string ErrorText { get; private set; } = string.Empty;

    /// <summary>Gets the diagnostic code of the last refused edit, or nothing.</summary>
    public string ErrorCode { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the last edit was refused.</summary>
    public bool HasError => this.ErrorText.Length > 0;

    /// <summary>Gets or sets the policy kind, an index in <see cref="Kinds"/>; -1 when the nodes' policies differ.</summary>
    /// <remarks>
    /// Choosing a kind writes the asset's policy of that kind, or that kind's default boundaries (see
    /// <see cref="LodPolicyShapes"/>), on every node; from mixed policies, any kind does.
    /// </remarks>
    public int Kind
    {
        get => this.kind;
        set
        {
            if (value >= 0 && value < this.Kinds.Count && value != this.kind)
            {
                this.PendingEdit = this.owner.SetLodPolicyAsync(LodPolicyShapes.Create(value, this.asset, this.lodCount), "Edit Level of Detail");
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
                this.ShowError(null, string.Create(CultureInfo.CurrentCulture, $"The LOD must be a whole number from 0 to {this.lodCount - 1}."));
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
    internal Task PendingEdit { get; private set; } = Task.CompletedTask;

    /// <summary>Shows the selected nodes' policy.</summary>
    /// <param name="inventory">The geometry's inventory, with the asset's policy and the LOD count.</param>
    /// <param name="nodes">The overrides of each selected node's geometry.</param>
    /// <param name="canEdit">Whether the policy can be edited now.</param>
    internal void Update(GeometrySurfaceMetadata inventory, IReadOnlyList<SourceOverrides> nodes, bool canEdit)
    {
        this.asset = inventory.LodPolicy;
        this.lodCount = inventory.LodCount;
        var policies = nodes.Select(overrides => overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var policy) ? policy : this.asset).ToList();
        var agreed = policies.TrueForAll(policy => policy.Equals(policies[0])) ? policies[0] : null;
        this.shown = agreed is null ? null : LodPolicyShapes.Fit(agreed, this.lodCount);
        this.kind = this.shown is null ? -1 : LodPolicyShapes.KindOf(this.shown);

        this.CanEdit = canEdit;
        this.IsFixed = this.shown is LodPolicy.Fixed;
        this.IsDistance = this.shown is LodPolicy.Distance;
        this.IsScreenSpaceError = this.shown is LodPolicy.ScreenSpaceError;
        this.FixedRange = string.Create(CultureInfo.CurrentCulture, $"of 0 to {this.lodCount - 1}");

        var overriding = nodes.Count(overrides => overrides.TryGet(SceneSchema.Geometry.LodPolicy, out _));

        // A single LOD always shows, so its policy changes nothing; an override, such as one kept from a geometry with
        // more LODs, still shows so it can be reset.
        this.IsAvailable = this.lodCount > 1 || overriding > 0;
        this.OverrideState = overriding == 0 ? OverrideState.None : overriding == nodes.Count ? OverrideState.Full : OverrideState.Partial;
        var reset = $"Reset to the asset's policy ({LodPolicyShapes.NameOf(this.asset)})";
        this.ResetToolTip = overriding == nodes.Count ? reset : string.Create(CultureInfo.CurrentCulture, $"{reset}, on the {overriding} of {nodes.Count} nodes that change it");
        this.Summary = SummaryOf(agreed is null, overriding, nodes.Count, this.lodCount, this.asset);
        this.UpdateTransitions();

        // The fields show the scene's values even when an edit was refused after they changed.
        this.OnPropertyChanged(nameof(this.Kind));
        this.OnPropertyChanged(nameof(this.FixedLod));
        this.OnPropertyChanged(nameof(this.Hysteresis));
    }

    /// <summary>Hides the rows: the selection has no shared geometry with a readable inventory.</summary>
    internal void Clear()
    {
        this.shown = null;
        this.kind = -1;
        this.IsAvailable = false;
        this.Transitions = [];
        this.ShowError(null);
    }

    /// <summary>Shows why an edit was refused, or clears it.</summary>
    /// <param name="error">The refusal, or <see langword="null"/> to clear it.</param>
    /// <param name="message">The message, when the refusal is the inspector's own.</param>
    internal void ShowError(SceneDiagnostic? error, string? message = null)
    {
        this.ErrorCode = error?.Code ?? string.Empty;
        this.ErrorText = error?.Message ?? message ?? string.Empty;
    }

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

    private static string SummaryOf(bool mixed, int overriding, int count, int lodCount, LodPolicy asset)
    {
        if (mixed)
        {
            return "Mixed: the selected nodes use different policies";
        }

        if (overriding == 0)
        {
            return string.Create(CultureInfo.CurrentCulture, $"{lodCount} {(lodCount == 1 ? "LOD" : "LODs")}, from the asset");
        }

        var source = $"the asset uses {LodPolicyShapes.NameOf(asset)}";
        return overriding == count ? $"Overridden; {source}" : string.Create(CultureInfo.CurrentCulture, $"Overridden on {overriding} of {count} nodes; {source}");
    }

    private void Write(LodPolicy policy)
    {
        if (!policy.Equals(this.shown))
        {
            this.PendingEdit = this.owner.SetLodPolicyAsync(policy, "Edit Level of Detail");
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
            this.Transitions = [.. Enumerable.Range(0, count).Select(index => new LodTransitionViewModel(this, index))];
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
    private Task ResetAsync() => this.owner.SetLodPolicyAsync(policy: null, "Reset Level of Detail");
}
