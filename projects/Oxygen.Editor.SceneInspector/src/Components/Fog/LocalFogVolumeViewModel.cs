// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Fog;

/// <summary>Owns local fog volume selection and command lifetime; typed bindings are the scalar state.</summary>
public sealed partial class LocalFogVolumeViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorFieldOverride unboundOverride = new(static () => { });
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<bool> enabledBinding = new(SceneProperties.LocalFogVolume.EnabledDescriptor);
    private readonly PropertyBinding<int> sortPriorityBinding = new(SceneProperties.LocalFogVolume.SortPriorityDescriptor);
    private readonly PropertyBinding<Vector3> albedoBinding = new(SceneProperties.LocalFogVolume.FogAlbedoDescriptor);
    private readonly PropertyBinding<Vector3> emissiveBinding = new(SceneProperties.LocalFogVolume.FogEmissiveDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="LocalFogVolumeViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public LocalFogVolumeViewModel(
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        if (authoringProvider is not null)
        {
            this.edits = new(authoringProvider, "Edit Local Fog Volume", this.RefreshValues);
        }

        this.Register(this.enabledBinding, nameof(this.Enabled));
        this.Register(this.RadialFogExtinction);
        this.Register(this.HeightFogExtinction);
        this.Register(this.HeightFogFalloff);
        this.Register(this.HeightFogOffset);
        this.Register(this.FogPhaseG);
        this.Register(this.sortPriorityBinding, nameof(this.SortPriority));
    }

    /// <summary>Gets or sets the albedo red channel.</summary>
    [ObservableProperty]
    public partial double AlbedoR { get; set; } = 1f;

    /// <summary>Gets or sets the albedo green channel.</summary>
    [ObservableProperty]
    public partial double AlbedoG { get; set; } = 1f;

    /// <summary>Gets or sets the albedo blue channel.</summary>
    [ObservableProperty]
    public partial double AlbedoB { get; set; } = 1f;

    /// <summary>Gets or sets the emissive red channel.</summary>
    [ObservableProperty]
    public partial double EmissiveR { get; set; }

    /// <summary>Gets or sets the emissive green channel.</summary>
    [ObservableProperty]
    public partial double EmissiveG { get; set; }

    /// <summary>Gets or sets the emissive blue channel.</summary>
    [ObservableProperty]
    public partial double EmissiveB { get; set; }

    /// <summary>Gets or sets a value indicating whether the volume is enabled, through its guarded binding.</summary>
    public bool Enabled
    {
        get => this.enabledBinding.Value;
        set => InspectorBindingRequests.Request(this.enabledBinding, value);
    }

    /// <summary>
    /// Gets or sets the composition order through its guarded binding; a number with no integer value shows the authored
    /// order again.
    /// </summary>
    public double SortPriority
    {
        get => this.sortPriorityBinding.Value;
        set
        {
            if (InspectorValues.TryToInt32(value, out var priority))
            {
                InspectorBindingRequests.Request(this.sortPriorityBinding, priority);
            }
            else
            {
                this.RefreshValues();
            }
        }
    }

    /// <summary>Gets the radial extinction binding and its mixed state.</summary>
    public NumberBinding<float> RadialFogExtinction { get; } = new(SceneProperties.LocalFogVolume.RadialFogExtinctionDescriptor);

    /// <summary>Gets the height extinction binding and its mixed state.</summary>
    public NumberBinding<float> HeightFogExtinction { get; } = new(SceneProperties.LocalFogVolume.HeightFogExtinctionDescriptor);

    /// <summary>Gets the height falloff binding and its mixed state.</summary>
    public NumberBinding<float> HeightFogFalloff { get; } = new(SceneProperties.LocalFogVolume.HeightFogFalloffDescriptor);

    /// <summary>Gets the height offset binding and its mixed state.</summary>
    public NumberBinding<float> HeightFogOffset { get; } = new(SceneProperties.LocalFogVolume.HeightFogOffsetDescriptor);

    /// <summary>Gets the phase anisotropy binding and its mixed state.</summary>
    public NumberBinding<float> FogPhaseG { get; } = new(SceneProperties.LocalFogVolume.FogPhaseGDescriptor);

    /// <summary>Gets radial extinction feedback.</summary>
    public InspectorFieldDiagnostic RadialFogExtinctionDiagnostic => this.Diagnostic(this.RadialFogExtinction.Id.Id);

    /// <summary>Gets height extinction feedback.</summary>
    public InspectorFieldDiagnostic HeightFogExtinctionDiagnostic => this.Diagnostic(this.HeightFogExtinction.Id.Id);

    /// <summary>Gets height falloff feedback.</summary>
    public InspectorFieldDiagnostic HeightFogFalloffDiagnostic => this.Diagnostic(this.HeightFogFalloff.Id.Id);

    /// <summary>Gets height offset feedback.</summary>
    public InspectorFieldDiagnostic HeightFogOffsetDiagnostic => this.Diagnostic(this.HeightFogOffset.Id.Id);

    /// <summary>Gets phase feedback.</summary>
    public InspectorFieldDiagnostic FogPhaseGDiagnostic => this.Diagnostic(this.FogPhaseG.Id.Id);

    /// <summary>Gets albedo feedback.</summary>
    public InspectorFieldDiagnostic AlbedoDiagnostic => this.Diagnostic(this.albedoBinding.Id.Id);

    /// <summary>Gets emissive feedback.</summary>
    public InspectorFieldDiagnostic EmissiveDiagnostic => this.Diagnostic(this.emissiveBinding.Id.Id);

    /// <summary>Gets sort priority feedback.</summary>
    public InspectorFieldDiagnostic SortPriorityDiagnostic => this.Diagnostic(this.sortPriorityBinding.Id.Id);

    /// <summary>Gets the enabled override bar.</summary>
    public InspectorFieldOverride EnabledOverride => this.Override(this.enabledBinding.Id.Id);

    /// <summary>Gets the radial extinction override bar.</summary>
    public InspectorFieldOverride RadialFogExtinctionOverride => this.Override(this.RadialFogExtinction.Id.Id);

    /// <summary>Gets the height extinction override bar.</summary>
    public InspectorFieldOverride HeightFogExtinctionOverride => this.Override(this.HeightFogExtinction.Id.Id);

    /// <summary>Gets the height falloff override bar.</summary>
    public InspectorFieldOverride HeightFogFalloffOverride => this.Override(this.HeightFogFalloff.Id.Id);

    /// <summary>Gets the height offset override bar.</summary>
    public InspectorFieldOverride HeightFogOffsetOverride => this.Override(this.HeightFogOffset.Id.Id);

    /// <summary>Gets the phase anisotropy override bar.</summary>
    public InspectorFieldOverride FogPhaseGOverride => this.Override(this.FogPhaseG.Id.Id);

    /// <summary>Gets the albedo override bar.</summary>
    public InspectorFieldOverride AlbedoOverride => this.Override(this.albedoBinding.Id.Id);

    /// <summary>Gets the emissive override bar.</summary>
    public InspectorFieldOverride EmissiveOverride => this.Override(this.emissiveBinding.Id.Id);

    /// <summary>Gets the sort priority override bar.</summary>
    public InspectorFieldOverride SortPriorityOverride => this.Override(this.sortPriorityBinding.Id.Id);

    /// <inheritdoc />
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Local Fog Volume";

    /// <inheritdoc />
    public override string Description => "A sphere of fog, 5 m in radius at unit scale, placed by the node's transform.";

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <summary>Gets completion of submitted volume edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc />
    public void BeginEditSession(string field, DroidNet.Controls.NumberBoxEditInteractionKind interaction) => this.edits?.Begin(field, interaction);

    /// <inheritdoc />
    public void CompleteEditSession(DroidNet.Controls.NumberBoxEditSessionEventArgs args) => this.edits?.Complete(args);

    /// <inheritdoc />
    public void EndEditSession(DroidNet.Controls.NumberBoxEditCompletionKind completion) => this.edits?.End(completion);

    /// <summary>Authors the complete linear albedo in one edit.</summary>
    /// <param name="color">The linear albedo.</param>
    public void SetAlbedo(Vector3 color)
    {
        this.SetDisplayColors(color, InspectorValues.ToVector3(this.EmissiveR, this.EmissiveG, this.EmissiveB));
        this.SubmitPerTarget(this.albedoBinding.Id, _ => color);
    }

    /// <summary>Authors the complete emitted luminance in one edit.</summary>
    /// <param name="color">The emitted luminance.</param>
    public void SetEmissive(Vector3 color)
    {
        this.SetDisplayColors(InspectorValues.ToVector3(this.AlbedoR, this.AlbedoG, this.AlbedoB), color);
        this.SubmitPerTarget(this.emissiveBinding.Id, _ => color);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        foreach (var registration in this.registrations)
        {
            registration.Dispose();
        }

        this.edits?.Dispose();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        var targets = items.Where(static node => node.Components.OfType<LocalFogVolumeComponent>().Any())
            .ToDictionary(this.ReferenceOf, static node => (object?)node.Components.OfType<LocalFogVolumeComponent>().First());
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }

        this.albedoBinding.UpdateFromModel(nodes, id => targets.GetValueOrDefault(id));
        this.emissiveBinding.UpdateFromModel(nodes, id => targets.GetValueOrDefault(id));
        this.SetDisplayColors(
            this.albedoBinding.HasValue ? this.albedoBinding.Value : Vector3.One,
            this.emissiveBinding.HasValue ? this.emissiveBinding.Value : Vector3.Zero);
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

    private static Vector3 WithChannel(Vector3 color, int channel, double value)
    {
        color[channel] = (float)value;
        return color;
    }

    partial void OnAlbedoRChanged(double value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 0, value);

    partial void OnAlbedoGChanged(double value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 1, value);

    partial void OnAlbedoBChanged(double value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 2, value);

    partial void OnEmissiveRChanged(double value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 0, value);

    partial void OnEmissiveGChanged(double value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 1, value);

    partial void OnEmissiveBChanged(double value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 2, value);

    private void ApplyChannel(PropertyId<Vector3> id, Func<LocalFogVolumeComponent, Vector3> read, int channel, double value)
    {
        if (!this.isApplyingEditorValues)
        {
            this.SubmitPerTarget(id, volume => WithChannel(read(volume), channel, value));
        }
    }

    private void SubmitPerTarget(PropertyId<Vector3> id, Func<LocalFogVolumeComponent, Vector3> color)
    {
        if (this.selectedItems is null)
        {
            return;
        }

        if (!this.IsInputEnabled)
        {
            this.RefreshValues();
            return;
        }

        var perTarget = new Dictionary<NodeReference, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<LocalFogVolumeComponent>().FirstOrDefault() is { } volume)
            {
                perTarget[this.ReferenceOf(node)] = PropertyEdit.SingleEdit(id, color(volume));
            }
        }

        this.edits?.Submit(perTarget);
    }

    private void SetDisplayColors(Vector3 albedo, Vector3 emissive)
    {
        this.isApplyingEditorValues = true;
        try
        {
            this.AlbedoR = albedo.X;
            this.AlbedoG = albedo.Y;
            this.AlbedoB = albedo.Z;
            this.EmissiveR = emissive.X;
            this.EmissiveG = emissive.Y;
            this.EmissiveB = emissive.Z;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }
    }

    private InspectorFieldDiagnostic Diagnostic(PropertyId id)
        => this.edits?.Diagnostics.Get(id) ?? this.unboundDiagnostic;

    /// <summary>Gets the override bar of a field.</summary>
    /// <param name="ids">The properties the field shows.</param>
    /// <returns>The field's bar.</returns>
    private InspectorFieldOverride Override(params PropertyId[] ids)
        => this.edits?.Overrides.Get(ids) ?? this.unboundOverride;

    private void Register<T>(PropertyBinding<T> binding, string? property = null)
        => this.registrations.Add(new InspectorBindingRegistration<T>(
            binding,
            this.edits,
            () => this.IsInputEnabled && !this.disposed,
            this.RefreshValues,
            property is null ? null : () => this.OnPropertyChanged(property)));

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }
}
