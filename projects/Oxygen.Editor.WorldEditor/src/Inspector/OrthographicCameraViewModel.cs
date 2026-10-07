// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns orthographic camera selection and command lifetime; typed bindings are the sole scalar state.</summary>
public sealed partial class OrthographicCameraViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<CameraAspectMode> aspectModeBinding = new(SceneDocumentCommandService.OrthographicCamera.AspectModeDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="OrthographicCameraViewModel"/> class.</summary>
    /// <param name="commandService">The existing camera command service.</param>
    /// <param name="commandContextProvider">The current captured document context.</param>
    public OrthographicCameraViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Camera", this.RefreshValues);
            this.edits.Diagnostics.Relate(this.NearPlane.Id.Id, this.FarPlane.Id.Id);
        }

        this.Register(this.OrthographicSize);
        this.Register(this.aspectModeBinding, nameof(this.AspectMode));
        this.Register(this.AspectRatio);
        this.Register(this.NearPlane);
        this.Register(this.FarPlane);
    }

    /// <summary>Gets the half-height binding and its mixed state.</summary>
    public PropertyBinding<float> OrthographicSize { get; } = new(SceneDocumentCommandService.OrthographicCamera.OrthographicSizeDescriptor);

    /// <summary>Gets or sets the framing policy through its guarded binding.</summary>
    public CameraAspectMode AspectMode
    {
        get => this.aspectModeBinding.Value;
        set => InspectorBindingRequests.Request(this.aspectModeBinding, value);
    }

    /// <summary>Gets the retained Fixed aspect-ratio binding and its mixed state.</summary>
    public PropertyBinding<float> AspectRatio { get; } = new(SceneDocumentCommandService.OrthographicCamera.AspectRatioDescriptor);

    /// <summary>Gets the near-plane binding and its mixed state.</summary>
    public PropertyBinding<float> NearPlane { get; } = new(SceneDocumentCommandService.OrthographicCamera.NearPlaneDescriptor);

    /// <summary>Gets the far-plane binding and its mixed state.</summary>
    public PropertyBinding<float> FarPlane { get; } = new(SceneDocumentCommandService.OrthographicCamera.FarPlaneDescriptor);

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

    /// <inheritdoc />
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Orthographic Camera";

    /// <inheritdoc />
    public override string Description => "Defines the orthographic volume, clipping planes, and framing.";

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
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        var targets = items.Where(static node => node.Components.OfType<OrthographicCamera>().Any())
            .ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<OrthographicCamera>().First());
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

    private InspectorFieldDiagnostic Diagnostic(Oxygen.Editor.Schemas.PropertyId id)
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
