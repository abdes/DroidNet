// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for V0.1 perspective camera inspector editing.
/// </summary>
public sealed partial class PerspectiveCameraViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();

    private readonly InspectorEditSessionCoordinator? edits;
    private readonly PropertyBinding<float> fieldOfViewBinding = new(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegreesDescriptor);
    private readonly PropertyBinding<float> nearPlaneBinding = new(SceneDocumentCommandService.PerspectiveCamera.NearPlaneDescriptor);
    private readonly PropertyBinding<float> farPlaneBinding = new(SceneDocumentCommandService.PerspectiveCamera.FarPlaneDescriptor);
    private readonly PropertyBinding<float> aspectRatioBinding = new(SceneDocumentCommandService.PerspectiveCamera.AspectRatioDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="PerspectiveCameraViewModel"/> class.
    /// </summary>
    /// <param name="commandService">Optional command service used to apply camera edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public PerspectiveCameraViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Camera", this.RefreshValues);
            this.edits.Diagnostics.Relate(this.nearPlaneBinding.Id.Id, this.farPlaneBinding.Id.Id);
        }

        this.fieldOfViewBinding.ValueRequested += this.OnCameraValueRequested;
        this.nearPlaneBinding.ValueRequested += this.OnCameraValueRequested;
        this.farPlaneBinding.ValueRequested += this.OnCameraValueRequested;
        this.aspectRatioBinding.ValueRequested += this.OnCameraValueRequested;
    }

    [ObservableProperty]
    public partial bool FieldOfViewIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float FieldOfView { get; set; }

    [ObservableProperty]
    public partial bool NearPlaneIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float NearPlane { get; set; }

    [ObservableProperty]
    public partial bool FarPlaneIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float FarPlane { get; set; }

    [ObservableProperty]
    public partial bool AspectRatioIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float AspectRatio { get; set; }

    /// <summary>Gets current diagnostics for FieldOfView.</summary>
    public InspectorFieldDiagnostic FieldOfViewDiagnostic => this.edits?.Diagnostics.Get(this.fieldOfViewBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for NearPlane.</summary>
    public InspectorFieldDiagnostic NearPlaneDiagnostic => this.edits?.Diagnostics.Get(this.nearPlaneBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for FarPlane.</summary>
    public InspectorFieldDiagnostic FarPlaneDiagnostic => this.edits?.Diagnostics.Get(this.farPlaneBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AspectRatio.</summary>
    public InspectorFieldDiagnostic AspectRatioDiagnostic => this.edits?.Diagnostics.Get(this.aspectRatioBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <inheritdoc/>
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Perspective Camera";

    /// <inheritdoc />
    public override string Description => "Defines projection, clipping planes, and aspect ratio.";

    /// <summary>Gets completion of submitted inspector edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc/>
    public void BeginEditSession(string field, DroidNet.Controls.NumberBoxEditInteractionKind interaction)
        => this.edits?.Begin(field, interaction);

    /// <inheritdoc/>
    public void CompleteEditSession(DroidNet.Controls.NumberBoxEditSessionEventArgs args)
        => this.edits?.Complete(args);

    /// <inheritdoc/>
    public void EndEditSession(DroidNet.Controls.NumberBoxEditCompletionKind completion)
        => this.edits?.End(completion);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.fieldOfViewBinding.ValueRequested -= this.OnCameraValueRequested;
        this.nearPlaneBinding.ValueRequested -= this.OnCameraValueRequested;
        this.farPlaneBinding.ValueRequested -= this.OnCameraValueRequested;
        this.aspectRatioBinding.ValueRequested -= this.OnCameraValueRequested;
        this.edits?.Dispose();
        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.edits?.Bind(items.Where(node => node.Components.Any(component => component is PerspectiveCamera)).Select(node => node.Id).ToArray());
        this.selectedItems = items;
        var targets = items
            .Select(static node => new { Node = node, Camera = node.Components.OfType<PerspectiveCamera>().FirstOrDefault() })
            .Where(static target => target.Camera is not null)
            .ToList();
        var nodeIds = targets.ConvertAll(static target => target.Node.Id);
        var targetsByNode = targets.ToDictionary(static target => target.Node.Id, static target => (object?)target.Camera);

        this.isApplyingEditorValues = true;
        try
        {
            this.UpdateBinding(this.fieldOfViewBinding, nodeIds, targetsByNode, value => this.FieldOfView = value, value => this.FieldOfViewIsIndeterminate = value);
            this.UpdateBinding(this.nearPlaneBinding, nodeIds, targetsByNode, value => this.NearPlane = value, value => this.NearPlaneIsIndeterminate = value);
            this.UpdateBinding(this.farPlaneBinding, nodeIds, targetsByNode, value => this.FarPlane = value, value => this.FarPlaneIsIndeterminate = value);
            this.UpdateBinding(this.aspectRatioBinding, nodeIds, targetsByNode, value => this.AspectRatio = value, value => this.AspectRatioIsIndeterminate = value);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }
    }

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }

    private void UpdateBinding(
        PropertyBinding<float> binding,
        IReadOnlyList<Guid> nodeIds,
        Dictionary<Guid, object?> targetsByNode,
        Action<float> setValue,
        Action<bool> setIndeterminate)
    {
        var previous = binding.HasValue ? (object?)binding.Value : null;
        var wasMixed = binding.IsMixed;
        binding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
        if (!Equals(previous, binding.HasValue ? binding.Value : null) || wasMixed != binding.IsMixed)
        {
            this.edits?.ModelChanged(binding.Id.Id);
        }

        setIndeterminate(binding.IsMixed);
        setValue(binding.HasValue ? binding.Value : 0f);
    }

    partial void OnFieldOfViewChanged(float value)
        => this.RequestCameraValue(this.fieldOfViewBinding, value);

    partial void OnNearPlaneChanged(float value)
        => this.RequestCameraValue(this.nearPlaneBinding, value);

    partial void OnFarPlaneChanged(float value)
        => this.RequestCameraValue(this.farPlaneBinding, value);

    partial void OnAspectRatioChanged(float value)
        => this.RequestCameraValue(this.aspectRatioBinding, value);

    private void RequestCameraValue(PropertyBinding<float> binding, float value)
    {
        if (this.isApplyingEditorValues)
        {
            return;
        }

        binding.Value = value;
    }

    private void OnCameraValueRequested(object? sender, PropertyBindingChangedEventArgs<float> args)
    {
        if (sender is not PropertyBinding<float> binding)
        {
            return;
        }

        this.ApplyCameraEdit(PropertyEdit.Single(binding.Id, args.NewValue));
    }

    private void ApplyCameraEdit(PropertyEdit edit)
    {
        if (!this.isApplyingEditorValues)
        {
            this.edits?.Submit(edit);
        }
    }
}
