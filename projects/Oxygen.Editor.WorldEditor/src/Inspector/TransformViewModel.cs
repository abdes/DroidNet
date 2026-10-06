// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns transform values and mixed/diagnostic presentation; the edit controller owns gesture lifetime.</summary>
public sealed partial class TransformViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly InspectorFieldDiagnostics diagnostics = new();
    private readonly TransformEditController controller;
    private readonly PropertyBinding<float> positionXBinding = new(SceneDocumentCommandService.Transform.PositionXDescriptor);
    private readonly PropertyBinding<float> positionYBinding = new(SceneDocumentCommandService.Transform.PositionYDescriptor);
    private readonly PropertyBinding<float> positionZBinding = new(SceneDocumentCommandService.Transform.PositionZDescriptor);
    private readonly PropertyBinding<float> rotationXBinding = new(SceneDocumentCommandService.Transform.RotationXDescriptor);
    private readonly PropertyBinding<float> rotationYBinding = new(SceneDocumentCommandService.Transform.RotationYDescriptor);
    private readonly PropertyBinding<float> rotationZBinding = new(SceneDocumentCommandService.Transform.RotationZDescriptor);
    private readonly PropertyBinding<float> scaleXBinding = new(SceneDocumentCommandService.Transform.ScaleXDescriptor);
    private readonly PropertyBinding<float> scaleYBinding = new(SceneDocumentCommandService.Transform.ScaleYDescriptor);
    private readonly PropertyBinding<float> scaleZBinding = new(SceneDocumentCommandService.Transform.ScaleZDescriptor);
    private bool isApplyingEditorChanges;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="TransformViewModel"/> class.</summary>
    /// <param name="loggerFactory">The scoped logging factory.</param>
    /// <param name="commandService">The existing transform command service.</param>
    /// <param name="commandContextProvider">The current document context provider.</param>
    public TransformViewModel(
        ILoggerFactory? loggerFactory = null,
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        this.LoggerFactory = loggerFactory;
        this.controller = new(loggerFactory, commandService, commandContextProvider, () => this.IsInputEnabled && !this.disposed, this.UpdateValues, this.diagnostics);
    }

    [ObservableProperty]
    public partial float PositionX { get; set; }

    [ObservableProperty]
    public partial bool PositionXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float PositionY { get; set; }

    [ObservableProperty]
    public partial bool PositionYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float PositionZ { get; set; }

    [ObservableProperty]
    public partial bool PositionZIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float RotationX { get; set; }

    [ObservableProperty]
    public partial bool RotationXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float RotationY { get; set; }

    [ObservableProperty]
    public partial bool RotationYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float RotationZ { get; set; }

    [ObservableProperty]
    public partial bool RotationZIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float ScaleX { get; set; }

    [ObservableProperty]
    public partial bool ScaleXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float ScaleY { get; set; }

    [ObservableProperty]
    public partial bool ScaleYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float ScaleZ { get; set; }

    [ObservableProperty]
    public partial bool ScaleZIsIndeterminate { get; set; }

    /// <summary>Gets the scoped factory used by numeric controls.</summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionXDiagnostic => this.diagnostics.Get(this.positionXBinding.Id.Id);

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionYDiagnostic => this.diagnostics.Get(this.positionYBinding.Id.Id);

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionZDiagnostic => this.diagnostics.Get(this.positionZBinding.Id.Id);

    /// <summary>Gets per-axis rotation feedback.</summary>
    public InspectorFieldDiagnostic RotationXDiagnostic => this.diagnostics.Get(this.rotationXBinding.Id.Id);

    /// <summary>Gets per-axis rotation feedback.</summary>
    public InspectorFieldDiagnostic RotationYDiagnostic => this.diagnostics.Get(this.rotationYBinding.Id.Id);

    /// <summary>Gets per-axis rotation feedback.</summary>
    public InspectorFieldDiagnostic RotationZDiagnostic => this.diagnostics.Get(this.rotationZBinding.Id.Id);

    /// <summary>Gets per-axis scale feedback.</summary>
    public InspectorFieldDiagnostic ScaleXDiagnostic => this.diagnostics.Get(this.scaleXBinding.Id.Id);

    /// <summary>Gets per-axis scale feedback.</summary>
    public InspectorFieldDiagnostic ScaleYDiagnostic => this.diagnostics.Get(this.scaleYBinding.Id.Id);

    /// <summary>Gets per-axis scale feedback.</summary>
    public InspectorFieldDiagnostic ScaleZDiagnostic => this.diagnostics.Get(this.scaleZBinding.Id.Id);

    /// <inheritdoc />
    public override string Header => "Transform";

    /// <inheritdoc />
    public override string Description => "Defines the position, rotation and scale of a Game Object along the X, Y and Z axis.";

    /// <summary>Gets existing position metadata.</summary>
    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    /// <summary>Gets existing rotation metadata.</summary>
    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    /// <summary>Gets existing scale metadata.</summary>
    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <summary>Gets completion of controller-owned in-flight requests.</summary>
    internal Task PendingEdits => this.controller.PendingEdits;

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.controller.BindSelection(items);
        this.isApplyingEditorChanges = true;
        try
        {
            this.UpdateBindingValue(this.positionXBinding, items, value => this.PositionX = value, mixed => this.PositionXIsIndeterminate = mixed);
            this.UpdateBindingValue(this.positionYBinding, items, value => this.PositionY = value, mixed => this.PositionYIsIndeterminate = mixed);
            this.UpdateBindingValue(this.positionZBinding, items, value => this.PositionZ = value, mixed => this.PositionZIsIndeterminate = mixed);
            this.UpdateBindingValue(this.rotationXBinding, items, value => this.RotationX = value, mixed => this.RotationXIsIndeterminate = mixed);
            this.UpdateBindingValue(this.rotationYBinding, items, value => this.RotationY = value, mixed => this.RotationYIsIndeterminate = mixed);
            this.UpdateBindingValue(this.rotationZBinding, items, value => this.RotationZ = value, mixed => this.RotationZIsIndeterminate = mixed);
            this.UpdateBindingValue(this.scaleXBinding, items, value => this.ScaleX = value, mixed => this.ScaleXIsIndeterminate = mixed);
            this.UpdateBindingValue(this.scaleYBinding, items, value => this.ScaleY = value, mixed => this.ScaleYIsIndeterminate = mixed);
            this.UpdateBindingValue(this.scaleZBinding, items, value => this.ScaleZ = value, mixed => this.ScaleZIsIndeterminate = mixed);
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.controller.Dispose();
    }

    /// <summary>Begins a captured-target component gesture.</summary>
    /// <param name="group">The vector group.</param>
    /// <param name="args">The original component interaction.</param>
    public void BeginEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args) => this.controller.BeginEditSession(group, args);

    /// <summary>Completes the existing gesture and relative-input policy.</summary>
    /// <param name="group">The vector group.</param>
    /// <param name="args">The original completion, including expression text.</param>
    public void CompleteEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args) => this.controller.CompleteEditSession(group, args);

    /// <summary>Publishes authoring feedback only for active edits, not model/template refreshes.</summary>
    /// <param name="group">The vector group.</param>
    /// <param name="args">The original control validation.</param>
    public void ReportControlValidation(TransformEditFieldGroup group, ValidationEventArgs<float> args)
    {
        if (!this.isApplyingEditorChanges)
        {
            this.controller.ReportControlValidation(group, args);
        }
    }

    partial void OnPositionXChanged(float value) => this.Apply("PositionX", TransformEditController.NewEdit(positionX: value));

    partial void OnPositionYChanged(float value) => this.Apply("PositionY", TransformEditController.NewEdit(positionY: value));

    partial void OnPositionZChanged(float value) => this.Apply("PositionZ", TransformEditController.NewEdit(positionZ: value));

    partial void OnRotationXChanged(float value) => this.Apply("RotationX", TransformEditController.NewEdit(rotationX: value));

    partial void OnRotationYChanged(float value) => this.Apply("RotationY", TransformEditController.NewEdit(rotationY: value));

    partial void OnRotationZChanged(float value) => this.Apply("RotationZ", TransformEditController.NewEdit(rotationZ: value));

    partial void OnScaleXChanged(float value) => this.Apply("ScaleX", TransformEditController.NewEdit(scaleX: value));

    partial void OnScaleYChanged(float value) => this.Apply("ScaleY", TransformEditController.NewEdit(scaleY: value));

    partial void OnScaleZChanged(float value) => this.Apply("ScaleZ", TransformEditController.NewEdit(scaleZ: value));

    private void Apply(string property, TransformEdit edit)
    {
        if (!this.isApplyingEditorChanges)
        {
            this.controller.ApplyTransformEdit(property, edit);
        }
    }

    private void UpdateBindingValue(PropertyBinding<float> binding, ICollection<SceneNode> items, Action<float> setValue, Action<bool> setMixed)
    {
        var nodes = items.Select(static node => node.Id).ToArray();
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<TransformComponent>().FirstOrDefault());
        this.RefreshSourceFeedback(binding, targets);
        binding.UpdateFromModel(nodes, id => targets.GetValueOrDefault(id));
        var value = binding.HasValue ? binding.Value : 0;
        if (binding.IsMixed && (binding.Id.Id == this.scaleXBinding.Id.Id || binding.Id.Id == this.scaleYBinding.Id.Id || binding.Id.Id == this.scaleZBinding.Id.Id))
        {
            value = TransformConverter.NormalizeScaleValue(value);
        }

        setValue(value);
        setMixed(binding.IsMixed);
    }
}
