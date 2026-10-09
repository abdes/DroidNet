// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns local fog volume selection and command lifetime; typed bindings are the scalar state.</summary>
public sealed partial class LocalFogVolumeViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<bool> enabledBinding = new(SceneDocumentCommandService.LocalFogVolume.EnabledDescriptor);
    private readonly PropertyBinding<int> sortPriorityBinding = new(SceneDocumentCommandService.LocalFogVolume.SortPriorityDescriptor);
    private readonly PropertyBinding<Vector3> albedoBinding = new(SceneDocumentCommandService.LocalFogVolume.FogAlbedoDescriptor);
    private readonly PropertyBinding<Vector3> emissiveBinding = new(SceneDocumentCommandService.LocalFogVolume.FogEmissiveDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="LocalFogVolumeViewModel"/> class.</summary>
    /// <param name="commandService">The existing scene command service.</param>
    /// <param name="commandContextProvider">The current captured document context.</param>
    public LocalFogVolumeViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Local Fog Volume", this.RefreshValues);
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
    public partial float AlbedoR { get; set; } = 1f;

    /// <summary>Gets or sets the albedo green channel.</summary>
    [ObservableProperty]
    public partial float AlbedoG { get; set; } = 1f;

    /// <summary>Gets or sets the albedo blue channel.</summary>
    [ObservableProperty]
    public partial float AlbedoB { get; set; } = 1f;

    /// <summary>Gets or sets the emissive red channel.</summary>
    [ObservableProperty]
    public partial float EmissiveR { get; set; }

    /// <summary>Gets or sets the emissive green channel.</summary>
    [ObservableProperty]
    public partial float EmissiveG { get; set; }

    /// <summary>Gets or sets the emissive blue channel.</summary>
    [ObservableProperty]
    public partial float EmissiveB { get; set; }

    /// <summary>Gets or sets volume enablement through its guarded binding.</summary>
    public bool Enabled
    {
        get => this.enabledBinding.Value;
        set => InspectorBindingRequests.Request(this.enabledBinding, value);
    }

    /// <summary>Gets or sets the composition order through its guarded binding.</summary>
    public float SortPriority
    {
        get => this.sortPriorityBinding.Value;
        set => InspectorBindingRequests.Request(this.sortPriorityBinding, (int)MathF.Round(value));
    }

    /// <summary>Gets the radial extinction binding and its mixed state.</summary>
    public PropertyBinding<float> RadialFogExtinction { get; } = new(SceneDocumentCommandService.LocalFogVolume.RadialFogExtinctionDescriptor);

    /// <summary>Gets the height extinction binding and its mixed state.</summary>
    public PropertyBinding<float> HeightFogExtinction { get; } = new(SceneDocumentCommandService.LocalFogVolume.HeightFogExtinctionDescriptor);

    /// <summary>Gets the height falloff binding and its mixed state.</summary>
    public PropertyBinding<float> HeightFogFalloff { get; } = new(SceneDocumentCommandService.LocalFogVolume.HeightFogFalloffDescriptor);

    /// <summary>Gets the height offset binding and its mixed state.</summary>
    public PropertyBinding<float> HeightFogOffset { get; } = new(SceneDocumentCommandService.LocalFogVolume.HeightFogOffsetDescriptor);

    /// <summary>Gets the phase anisotropy binding and its mixed state.</summary>
    public PropertyBinding<float> FogPhaseG { get; } = new(SceneDocumentCommandService.LocalFogVolume.FogPhaseGDescriptor);

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
        this.SetDisplayColors(color, new(this.EmissiveR, this.EmissiveG, this.EmissiveB));
        this.SubmitPerTarget(this.albedoBinding.Id, _ => color);
    }

    /// <summary>Authors the complete emitted luminance in one edit.</summary>
    /// <param name="color">The emitted luminance.</param>
    public void SetEmissive(Vector3 color)
    {
        this.SetDisplayColors(new(this.AlbedoR, this.AlbedoG, this.AlbedoB), color);
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
            .ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<LocalFogVolumeComponent>().First());
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

    private static Vector3 WithChannel(Vector3 color, int channel, float value)
    {
        color[channel] = value;
        return color;
    }

    partial void OnAlbedoRChanged(float value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 0, value);

    partial void OnAlbedoGChanged(float value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 1, value);

    partial void OnAlbedoBChanged(float value) => this.ApplyChannel(this.albedoBinding.Id, static v => v.FogAlbedo, 2, value);

    partial void OnEmissiveRChanged(float value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 0, value);

    partial void OnEmissiveGChanged(float value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 1, value);

    partial void OnEmissiveBChanged(float value) => this.ApplyChannel(this.emissiveBinding.Id, static v => v.FogEmissive, 2, value);

    private void ApplyChannel(PropertyId<Vector3> id, Func<LocalFogVolumeComponent, Vector3> read, int channel, float value)
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

        var perTarget = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<LocalFogVolumeComponent>().FirstOrDefault() is { } volume)
            {
                perTarget[node.Id] = PropertyEdit.SingleEdit(id, color(volume));
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
