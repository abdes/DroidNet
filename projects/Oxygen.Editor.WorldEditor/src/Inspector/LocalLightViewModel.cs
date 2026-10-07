// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Shared point/spot light inspector: common light, shadow and emission fields.</summary>
public abstract partial class LocalLightViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly PropertyBinding<Vector3> colorBinding;
    private readonly PropertyBinding<bool> affectsWorldBinding;
    private readonly PropertyBinding<bool> castsShadowsBinding;
    private readonly PropertyBinding<bool> contactShadowsBinding;
    private readonly PropertyBinding<ShadowResolutionHint> shadowResolutionHintBinding;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="LocalLightViewModel"/> class.</summary>
    /// <param name="descriptors">The kind-qualified descriptor catalog.</param>
    /// <param name="label">The history label.</param>
    /// <param name="commandService">Optional command service used to apply light edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    private protected LocalLightViewModel(
        LocalLightDescriptors descriptors,
        string label,
        ISceneDocumentCommandService? commandService,
        Func<SceneDocumentCommandContext?>? commandContextProvider)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, label, this.RefreshValues);
        }

        this.colorBinding = new(descriptors.ColorDescriptor);
        this.LuminousFluxLumens = this.Register(new PropertyBinding<float>(descriptors.LuminousFluxLumensDescriptor));
        this.Range = this.Register(new PropertyBinding<float>(descriptors.RangeDescriptor));
        this.SourceRadius = this.Register(new PropertyBinding<float>(descriptors.SourceRadiusDescriptor));
        this.affectsWorldBinding = this.Register(new PropertyBinding<bool>(descriptors.AffectsWorldDescriptor), nameof(this.AffectsWorld));
        this.ExposureCompensation = this.Register(new PropertyBinding<float>(descriptors.ExposureCompensationDescriptor));
        this.castsShadowsBinding = this.Register(new PropertyBinding<bool>(descriptors.CastsShadowsDescriptor), nameof(this.CastsShadows));
        this.contactShadowsBinding = this.Register(new PropertyBinding<bool>(descriptors.ContactShadowsDescriptor), nameof(this.ContactShadows));
        this.shadowResolutionHintBinding = this.Register(new PropertyBinding<ShadowResolutionHint>(descriptors.ShadowResolutionHintDescriptor), nameof(this.ShadowResolutionHint));
        this.ShadowBias = this.Register(new PropertyBinding<float>(descriptors.ShadowBiasDescriptor));
        this.ShadowNormalBias = this.Register(new PropertyBinding<float>(descriptors.ShadowNormalBiasDescriptor));
    }

    /// <summary>Gets or sets the linear red color channel.</summary>
    [ObservableProperty]
    public partial float ColorR { get; set; } = 1f;

    /// <summary>Gets or sets the linear green color channel.</summary>
    [ObservableProperty]
    public partial float ColorG { get; set; } = 1f;

    /// <summary>Gets or sets the linear blue color channel.</summary>
    [ObservableProperty]
    public partial float ColorB { get; set; } = 1f;

    /// <summary>Gets the luminous flux binding, in lumens.</summary>
    public PropertyBinding<float> LuminousFluxLumens { get; }

    /// <summary>Gets the influence range binding, in meters.</summary>
    public PropertyBinding<float> Range { get; }

    /// <summary>Gets the source radius binding, in meters.</summary>
    public PropertyBinding<float> SourceRadius { get; }

    /// <summary>Gets or sets a value indicating whether the light contributes to scene lighting.</summary>
    public bool AffectsWorld
    {
        get => this.affectsWorldBinding.Value;
        set => InspectorBindingRequests.Request(this.affectsWorldBinding, value);
    }

    /// <summary>Gets the exposure compensation binding, in EV.</summary>
    public PropertyBinding<float> ExposureCompensation { get; }

    /// <summary>Gets or sets a value indicating whether the light casts shadows.</summary>
    public bool CastsShadows
    {
        get => this.castsShadowsBinding.Value;
        set => InspectorBindingRequests.Request(this.castsShadowsBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether contact shadows are enabled.</summary>
    public bool ContactShadows
    {
        get => this.contactShadowsBinding.Value;
        set => InspectorBindingRequests.Request(this.contactShadowsBinding, value);
    }

    /// <summary>Gets or sets the shadow resolution hint.</summary>
    public ShadowResolutionHint ShadowResolutionHint
    {
        get => this.shadowResolutionHintBinding.Value;
        set => InspectorBindingRequests.Request(this.shadowResolutionHintBinding, value);
    }

    /// <summary>Gets the shadow depth bias binding.</summary>
    public PropertyBinding<float> ShadowBias { get; }

    /// <summary>Gets the shadow normal bias binding.</summary>
    public PropertyBinding<float> ShadowNormalBias { get; }

    /// <summary>Gets the shadow resolution options offered by the inspector.</summary>
    public IReadOnlyList<ShadowResolutionHint> ShadowResolutionOptions { get; } = Enum.GetValues<ShadowResolutionHint>();

    /// <summary>Gets color feedback.</summary>
    public InspectorFieldDiagnostic ColorDiagnostic => this.Diagnostic(this.colorBinding.Id.Id);

    /// <summary>Gets luminous flux feedback.</summary>
    public InspectorFieldDiagnostic LuminousFluxLumensDiagnostic => this.Diagnostic(this.LuminousFluxLumens.Id.Id);

    /// <summary>Gets range feedback.</summary>
    public InspectorFieldDiagnostic RangeDiagnostic => this.Diagnostic(this.Range.Id.Id);

    /// <summary>Gets source radius feedback.</summary>
    public InspectorFieldDiagnostic SourceRadiusDiagnostic => this.Diagnostic(this.SourceRadius.Id.Id);

    /// <summary>Gets exposure compensation feedback.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.Diagnostic(this.ExposureCompensation.Id.Id);

    /// <summary>Gets shadow depth bias feedback.</summary>
    public InspectorFieldDiagnostic ShadowBiasDiagnostic => this.Diagnostic(this.ShadowBias.Id.Id);

    /// <summary>Gets shadow normal bias feedback.</summary>
    public InspectorFieldDiagnostic ShadowNormalBiasDiagnostic => this.Diagnostic(this.ShadowNormalBias.Id.Id);

    /// <inheritdoc />
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <summary>Gets completion of submitted inspector edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc />
    public void BeginEditSession(string field, DroidNet.Controls.NumberBoxEditInteractionKind interaction) => this.edits?.Begin(field, interaction);

    /// <inheritdoc />
    public void CompleteEditSession(DroidNet.Controls.NumberBoxEditSessionEventArgs args) => this.edits?.Complete(args);

    /// <inheritdoc />
    public void EndEditSession(DroidNet.Controls.NumberBoxEditCompletionKind completion) => this.edits?.End(completion);

    /// <summary>Authors the complete linear RGB light color in one edit.</summary>
    /// <param name="color">The linear RGB light color.</param>
    public void SetColor(Vector3 color)
    {
        this.isApplyingEditorValues = true;
        try
        {
            this.SetDisplayColor(color);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.SubmitPerTarget(_ => color);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        var targets = items
            .Select(node => (node, light: this.FindLight(node)))
            .Where(static target => target.light is not null)
            .ToDictionary(static target => target.node.Id, static target => (object?)target.light);
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }

        this.colorBinding.UpdateFromModel(nodes, id => targets.GetValueOrDefault(id));
        this.isApplyingEditorValues = true;
        try
        {
            this.SetDisplayColor(this.colorBinding.HasValue ? this.colorBinding.Value : Vector3.One);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }
    }

    /// <summary>Releases binding subscriptions and the edit coordinator.</summary>
    /// <param name="disposing">Whether managed resources are being released.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        if (!disposing)
        {
            return;
        }

        foreach (var registration in this.registrations)
        {
            registration.Dispose();
        }

        this.edits?.Dispose();
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

    /// <summary>Finds the edited light on one node.</summary>
    /// <param name="node">The selected node.</param>
    /// <returns>The light, or null when the node has none of this kind.</returns>
    private protected abstract LightComponent? FindLight(SceneNode node);

    /// <summary>Registers one typed binding with the shared edit coordinator.</summary>
    /// <typeparam name="T">The authored value type.</typeparam>
    /// <param name="binding">The binding to register.</param>
    /// <param name="property">The presented property that mirrors the binding, if any.</param>
    /// <returns>The registered binding.</returns>
    private protected PropertyBinding<T> Register<T>(PropertyBinding<T> binding, string? property = null)
    {
        this.registrations.Add(new InspectorBindingRegistration<T>(
            binding,
            this.edits,
            () => this.IsInputEnabled && !this.disposed,
            this.RefreshValues,
            property is null ? null : () => this.OnPropertyChanged(property)));
        return binding;
    }

    /// <summary>Gets the diagnostic for one property.</summary>
    /// <param name="id">The property id.</param>
    /// <returns>The current diagnostic.</returns>
    private protected InspectorFieldDiagnostic Diagnostic(PropertyId id)
        => this.edits?.Diagnostics.Get(id) ?? this.unboundDiagnostic;

    partial void OnColorRChanged(float value) => this.ApplyColorAxisEdit(0, value);

    partial void OnColorGChanged(float value) => this.ApplyColorAxisEdit(1, value);

    partial void OnColorBChanged(float value) => this.ApplyColorAxisEdit(2, value);

    private void ApplyColorAxisEdit(int axis, float value)
    {
        if (this.isApplyingEditorValues)
        {
            return;
        }

        this.SubmitPerTarget(light =>
        {
            var color = light.Color;
            color[axis] = value;
            return color;
        });
    }

    private void SubmitPerTarget(Func<LightComponent, Vector3> color)
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
            if (this.FindLight(node) is { } light)
            {
                perTarget[node.Id] = PropertyEdit.SingleEdit(this.colorBinding.Id, color(light));
            }
        }

        this.edits?.Submit(perTarget);
    }

    private void SetDisplayColor(Vector3 color)
    {
        this.ColorR = color.X;
        this.ColorG = color.Y;
        this.ColorB = color.Z;
    }

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }
}
