// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns camera selection and command lifetime; typed bindings are the sole scalar state.</summary>
public sealed partial class PerspectiveCameraViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly InspectorBindingRegistration<float>[] registrations;
    private readonly InspectorBindingRegistration<float> fieldOfViewRegistration;
    private readonly InspectorBindingRegistration<float> aspectRatioRegistration;
    private readonly InspectorBindingRegistration<float> nearPlaneRegistration;
    private readonly InspectorBindingRegistration<float> farPlaneRegistration;
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="PerspectiveCameraViewModel"/> class.</summary>
    /// <param name="commandService">The existing camera command service.</param>
    /// <param name="commandContextProvider">The current captured document context.</param>
    public PerspectiveCameraViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Camera", this.RefreshValues);
            this.edits.Diagnostics.Relate(this.NearPlane.Id.Id, this.FarPlane.Id.Id);
        }

        this.fieldOfViewRegistration = this.Register(this.FieldOfView);
        this.aspectRatioRegistration = this.Register(this.AspectRatio);
        this.nearPlaneRegistration = this.Register(this.NearPlane);
        this.farPlaneRegistration = this.Register(this.FarPlane);
        this.registrations = [this.fieldOfViewRegistration, this.aspectRatioRegistration, this.nearPlaneRegistration, this.farPlaneRegistration];
    }

    /// <summary>Gets the degree-valued FOV binding and its mixed state.</summary>
    public PropertyBinding<float> FieldOfView { get; } = new(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegreesDescriptor);

    /// <summary>Gets the aspect-ratio binding and its mixed state.</summary>
    public PropertyBinding<float> AspectRatio { get; } = new(SceneDocumentCommandService.PerspectiveCamera.AspectRatioDescriptor);

    /// <summary>Gets the near-plane binding and its mixed state.</summary>
    public PropertyBinding<float> NearPlane { get; } = new(SceneDocumentCommandService.PerspectiveCamera.NearPlaneDescriptor);

    /// <summary>Gets the far-plane binding and its mixed state.</summary>
    public PropertyBinding<float> FarPlane { get; } = new(SceneDocumentCommandService.PerspectiveCamera.FarPlaneDescriptor);

    /// <summary>Gets FOV feedback.</summary>
    public InspectorFieldDiagnostic FieldOfViewDiagnostic => this.edits?.Diagnostics.Get(this.FieldOfView.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets aspect-ratio feedback.</summary>
    public InspectorFieldDiagnostic AspectRatioDiagnostic => this.edits?.Diagnostics.Get(this.AspectRatio.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets related near-plane feedback.</summary>
    public InspectorFieldDiagnostic NearPlaneDiagnostic => this.edits?.Diagnostics.Get(this.NearPlane.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets related far-plane feedback.</summary>
    public InspectorFieldDiagnostic FarPlaneDiagnostic => this.edits?.Diagnostics.Get(this.FarPlane.Id.Id) ?? this.unboundDiagnostic;

    /// <inheritdoc />
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Perspective Camera";

    /// <inheritdoc />
    public override string Description => "Defines projection, clipping planes, and aspect ratio.";

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
        this.fieldOfViewRegistration.Dispose();
        this.aspectRatioRegistration.Dispose();
        this.nearPlaneRegistration.Dispose();
        this.farPlaneRegistration.Dispose();

        this.edits?.Dispose();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.edits?.Bind(items.Where(node => node.Components.OfType<PerspectiveCamera>().Any()).Select(node => node.Id).ToArray());
        this.selectedItems = items;
        var targets = items.Where(node => node.Components.OfType<PerspectiveCamera>().Any())
            .ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<PerspectiveCamera>().First());
        var nodes = targets.Keys.ToArray();
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }
    }

    private InspectorBindingRegistration<float> Register(PropertyBinding<float> binding)
        => new(binding, this.edits, () => this.IsInputEnabled && !this.disposed, this.RefreshValues);

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }
}
