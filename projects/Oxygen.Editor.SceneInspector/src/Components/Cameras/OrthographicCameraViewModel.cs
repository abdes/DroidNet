// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Cameras;

/// <summary>Owns orthographic camera selection and command lifetime; typed bindings are the sole scalar state.</summary>
public sealed partial class OrthographicCameraViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<CameraAspectMode> aspectModeBinding = new(SceneProperties.OrthographicCamera.AspectModeDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="OrthographicCameraViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public OrthographicCameraViewModel(
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        this.StartingCamera = new(authoringProvider);
        if (authoringProvider is not null)
        {
            this.edits = new(authoringProvider, "Edit Camera", this.RefreshValues);
            this.edits.Diagnostics.Relate(this.NearPlane.Id.Id, this.FarPlane.Id.Id);
        }

        this.Register(this.OrthographicSize);
        this.Register(this.aspectModeBinding, nameof(this.AspectMode));
        this.Register(this.AspectRatio);
        this.Register(this.NearPlane);
        this.Register(this.FarPlane);
        this.Register(this.ApertureF);
        this.Register(this.ShutterRate);
        this.Register(this.Iso);
    }

    /// <summary>Gets the Starting camera check box, which edits the scene's starting camera.</summary>
    public StartingCameraToggle StartingCamera { get; }

    /// <summary>Gets the half-height binding and its mixed state.</summary>
    public NumberBinding<float> OrthographicSize { get; } = new(SceneProperties.OrthographicCamera.OrthographicSizeDescriptor);

    /// <summary>Gets or sets the framing policy through its guarded binding.</summary>
    public CameraAspectMode AspectMode
    {
        get => this.aspectModeBinding.Value;
        set => InspectorBindingRequests.Request(this.aspectModeBinding, value);
    }

    /// <summary>Gets the retained Fixed aspect-ratio binding and its mixed state.</summary>
    public NumberBinding<float> AspectRatio { get; } = new(SceneProperties.OrthographicCamera.AspectRatioDescriptor);

    /// <summary>Gets the near-plane binding and its mixed state.</summary>
    public NumberBinding<float> NearPlane { get; } = new(SceneProperties.OrthographicCamera.NearPlaneDescriptor);

    /// <summary>Gets the far-plane binding and its mixed state.</summary>
    public NumberBinding<float> FarPlane { get; } = new(SceneProperties.OrthographicCamera.FarPlaneDescriptor);

    /// <summary>Gets the aperture f-number binding and its mixed state.</summary>
    public NumberBinding<float> ApertureF { get; } = new(SceneProperties.OrthographicCamera.ApertureFDescriptor);

    /// <summary>Gets the shutter-rate binding and its mixed state.</summary>
    public NumberBinding<float> ShutterRate { get; } = new(SceneProperties.OrthographicCamera.ShutterRateDescriptor);

    /// <summary>Gets the ISO binding and its mixed state.</summary>
    public NumberBinding<float> Iso { get; } = new(SceneProperties.OrthographicCamera.IsoDescriptor);

    /// <summary>Gets the framing policies offered by the inspector.</summary>
    public IReadOnlyList<CameraAspectMode> AspectModeOptions { get; } = Enum.GetValues<CameraAspectMode>();

    /// <summary>Gets orthographic size feedback.</summary>
    public InspectorFieldDiagnostic OrthographicSizeDiagnostic => this.Diagnostic(this.OrthographicSize.Id.Id);

    /// <summary>Gets aspect-ratio feedback.</summary>
    public InspectorFieldDiagnostic AspectRatioDiagnostic => this.Diagnostic(this.AspectRatio.Id.Id);

    /// <summary>Gets related near-plane feedback.</summary>
    public InspectorFieldDiagnostic NearPlaneDiagnostic => this.Diagnostic(this.NearPlane.Id.Id);

    /// <summary>Gets related far-plane feedback.</summary>
    public InspectorFieldDiagnostic FarPlaneDiagnostic => this.Diagnostic(this.FarPlane.Id.Id);

    /// <summary>Gets aperture feedback.</summary>
    public InspectorFieldDiagnostic ApertureFDiagnostic => this.Diagnostic(this.ApertureF.Id.Id);

    /// <summary>Gets shutter-rate feedback.</summary>
    public InspectorFieldDiagnostic ShutterRateDiagnostic => this.Diagnostic(this.ShutterRate.Id.Id);

    /// <summary>Gets ISO feedback.</summary>
    public InspectorFieldDiagnostic IsoDiagnostic => this.Diagnostic(this.Iso.Id.Id);

    /// <inheritdoc />
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Orthographic Camera";

    /// <inheritdoc />
    public override string Description => "Defines the orthographic volume, clipping planes, framing, and physical exposure.";

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <summary>Gets completion of submitted camera edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc />
    public void BeginEditSession(string field, DroidNet.Controls.NumberBoxEditInteractionKind interaction) => this.edits?.Begin(field, interaction);

    /// <inheritdoc />
    public void CompleteEditSession(DroidNet.Controls.NumberBoxEditSessionEventArgs args) => this.edits?.Complete(args);

    /// <inheritdoc />
    public void EndEditSession(DroidNet.Controls.NumberBoxEditCompletionKind completion) => this.edits?.End(completion);

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
        this.StartingCamera.Dispose();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        var targets = items.Where(static node => node.Components.OfType<OrthographicCamera>().Any())
            .ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<OrthographicCamera>().First());
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        this.StartingCamera.Bind([.. items.Where(node => targets.ContainsKey(node.Id))]);
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

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
