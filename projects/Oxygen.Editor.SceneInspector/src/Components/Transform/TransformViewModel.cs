// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.SceneInspector.Components.Transform;

/// <summary>Owns transform values and mixed/diagnostic presentation; the edit controller owns gesture lifetime.</summary>
/// <remarks>
/// The section also edits Ignore Parent Transform, a node flag held by the node-flags component, since it decides how
/// the transform composes with the parent's.
/// </remarks>
public sealed partial class TransformViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly InspectorFieldDiagnostics diagnostics = new();
    private readonly TransformEditController controller;
    private readonly NumberBinding<float> positionXBinding = new(SceneProperties.Transform.PositionXDescriptor);
    private readonly NumberBinding<float> positionYBinding = new(SceneProperties.Transform.PositionYDescriptor);
    private readonly NumberBinding<float> positionZBinding = new(SceneProperties.Transform.PositionZDescriptor);
    private readonly NumberBinding<float> scaleXBinding = new(SceneProperties.Transform.ScaleXDescriptor);
    private readonly NumberBinding<float> scaleYBinding = new(SceneProperties.Transform.ScaleYDescriptor);
    private readonly NumberBinding<float> scaleZBinding = new(SceneProperties.Transform.ScaleZDescriptor);
    private readonly PropertyBinding<bool> ignoreParentTransformBinding = new(SceneProperties.NodeFlags.IgnoreParentTransformDescriptor);
    private readonly InspectorEditSessionCoordinator? flagEdits;
    private readonly InspectorBindingRegistration<bool> ignoreParentTransformRegistration;
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorChanges;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="TransformViewModel"/> class.</summary>
    /// <param name="loggerFactory">The scoped logging factory.</param>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public TransformViewModel(
        ILoggerFactory? loggerFactory = null,
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        this.LoggerFactory = loggerFactory;
        this.controller = new(loggerFactory, authoringProvider ?? (static () => null), () => this.IsInputEnabled && !this.disposed, this.UpdateValues, this.diagnostics);
        if (authoringProvider is not null)
        {
            this.flagEdits = new(authoringProvider, "Edit Transform", this.RefreshFlagValues);
        }

        this.ignoreParentTransformRegistration = new(
            this.ignoreParentTransformBinding,
            this.flagEdits,
            () => this.IsInputEnabled && !this.disposed,
            this.RefreshFlagValues,
            () => this.OnPropertyChanged(nameof(this.IgnoreParentTransform)));
    }

    [ObservableProperty]
    public partial double PositionX { get; set; }

    [ObservableProperty]
    public partial bool PositionXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double PositionY { get; set; }

    [ObservableProperty]
    public partial bool PositionYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double PositionZ { get; set; }

    [ObservableProperty]
    public partial bool PositionZIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double RotationX { get; set; }

    [ObservableProperty]
    public partial bool RotationXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double RotationY { get; set; }

    [ObservableProperty]
    public partial bool RotationYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double RotationZ { get; set; }

    [ObservableProperty]
    public partial bool RotationZIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double ScaleX { get; set; }

    [ObservableProperty]
    public partial bool ScaleXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double ScaleY { get; set; }

    [ObservableProperty]
    public partial bool ScaleYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial double ScaleZ { get; set; }

    [ObservableProperty]
    public partial bool ScaleZIsIndeterminate { get; set; }

    /// <summary>Gets or sets a value indicating whether the node's world transform ignores its parent's.</summary>
    public bool IgnoreParentTransform
    {
        get => this.ignoreParentTransformBinding.Value;
        set => InspectorBindingRequests.Request(this.ignoreParentTransformBinding, value);
    }

    /// <summary>Gets the scoped factory used by numeric controls.</summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionXDiagnostic => this.diagnostics.Get(this.positionXBinding.Id.Id);

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionYDiagnostic => this.diagnostics.Get(this.positionYBinding.Id.Id);

    /// <summary>Gets per-axis position feedback.</summary>
    public InspectorFieldDiagnostic PositionZDiagnostic => this.diagnostics.Get(this.positionZBinding.Id.Id);

    /// <summary>Gets the rotation feedback: the rotation is one property, whose message names the axis.</summary>
    public InspectorFieldDiagnostic RotationDiagnostic => this.diagnostics.Get(SceneProperties.Transform.RotationDescriptor.Id);

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
    public InspectorFieldState PositionProperty { get; } = new() { Name = "Position" };

    /// <summary>Gets existing rotation metadata.</summary>
    public InspectorFieldState RotationProperty { get; } = new() { Name = "Rotation" };

    /// <summary>Gets existing scale metadata.</summary>
    public InspectorFieldState ScaleProperty { get; } = new() { Name = "Scale" };

    /// <summary>Gets completion of controller-owned in-flight requests and of submitted flag edits.</summary>
    internal Task PendingEdits => Task.WhenAll(this.controller.PendingEdits, this.flagEdits?.Pending ?? Task.CompletedTask);

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
            this.UpdateRotationValues(items);
            this.UpdateBindingValue(this.scaleXBinding, items, value => this.ScaleX = value, mixed => this.ScaleXIsIndeterminate = mixed);
            this.UpdateBindingValue(this.scaleYBinding, items, value => this.ScaleY = value, mixed => this.ScaleYIsIndeterminate = mixed);
            this.UpdateBindingValue(this.scaleZBinding, items, value => this.ScaleZ = value, mixed => this.ScaleZIsIndeterminate = mixed);
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }

        this.UpdateFlagValues(items);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.controller.Dispose();
        this.ignoreParentTransformRegistration.Dispose();
        this.flagEdits?.Dispose();
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
    public void ReportControlValidation(TransformEditFieldGroup group, ValidationEventArgs<double> args)
    {
        if (!this.isApplyingEditorChanges)
        {
            this.controller.ReportControlValidation(group, args);
        }
    }

    partial void OnPositionXChanged(double value) => this.Apply("PositionX", value);

    partial void OnPositionYChanged(double value) => this.Apply("PositionY", value);

    partial void OnPositionZChanged(double value) => this.Apply("PositionZ", value);

    partial void OnRotationXChanged(double value) => this.Apply("RotationX", value);

    partial void OnRotationYChanged(double value) => this.Apply("RotationY", value);

    partial void OnRotationZChanged(double value) => this.Apply("RotationZ", value);

    partial void OnScaleXChanged(double value) => this.Apply("ScaleX", value);

    partial void OnScaleYChanged(double value) => this.Apply("ScaleY", value);

    partial void OnScaleZChanged(double value) => this.Apply("ScaleZ", value);

    private void Apply(string property, double value)
    {
        if (!this.isApplyingEditorChanges)
        {
            this.controller.ApplyTransformEdit(property, value);
        }
    }

    private void UpdateFlagValues(ICollection<SceneNode> items)
    {
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Flags);
        var nodes = targets.Keys.ToArray();
        this.flagEdits?.Bind(nodes);
        this.selectedItems = items;
        this.ignoreParentTransformRegistration.Refresh(nodes, id => targets.GetValueOrDefault(id));
    }

    private void RefreshFlagValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateFlagValues(items);
        }
    }

    // The rotation is one property; each axis shows the first node's angle and is mixed when the nodes' angles differ.
    private void UpdateRotationValues(ICollection<SceneNode> items)
    {
        var descriptor = SceneProperties.Transform.RotationDescriptor;
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Transform);
        this.RefreshSourceFeedback(descriptor, targets);
        var angles = items.Select(node => descriptor.Read(node.Transform)).ToList();
        var first = angles.Count > 0 ? angles[0] : Vector3.Zero;
        this.RotationX = first.X;
        this.RotationXIsIndeterminate = Mixed(0);
        this.RotationY = first.Y;
        this.RotationYIsIndeterminate = Mixed(1);
        this.RotationZ = first.Z;
        this.RotationZIsIndeterminate = Mixed(2);

        bool Mixed(int axis) => angles.Exists(value => !value[axis].Equals(angles[0][axis]));
    }

    private void UpdateBindingValue(NumberBinding<float> binding, ICollection<SceneNode> items, Action<float> setValue, Action<bool> setMixed)
    {
        var nodes = items.Select(static node => node.Id).ToArray();
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Transform);
        this.RefreshSourceFeedback(binding.Descriptor, targets);
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
