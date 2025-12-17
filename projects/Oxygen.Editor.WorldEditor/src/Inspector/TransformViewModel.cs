// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Messages;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// ViewModel for editing the transform properties (position, rotation, scale) of selected SceneNode instances.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class TransformViewModel(ILoggerFactory? loggerFactory = null, IMessenger? messenger = null) : ComponentPropertyEditor
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<TransformViewModel>();

    private readonly IMessenger? messengerSvc = messenger;

    // Keep track of the current selection so property-change handlers can apply edits back
    // to the selected SceneNode instances.
    private ICollection<SceneNode>? selectedItems;

    // Guard against re-entrant updates when applying changes from the view back to the model.
    private bool isApplyingEditorChanges;

    /// <summary>
    ///     Gets exposes the configured <see cref="ILoggerFactory"/> for views to bind to (read-only).
    /// </summary>
    public ILoggerFactory? LoggerFactory => loggerFactory;

    /// <summary>
    ///     Gets or sets the X position of the game object in the scene. The position can be abosulte
    ///     (relative to the scene) or relative to the parent object.
    /// </summary>
    // Position values: non-nullable backing values for VectorBox
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

    // Rotation values
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

    // Scale values
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

    /// <inheritdoc />
    public override string Header => "Transform";

    /// <inheritdoc />
    public override string Description =>
        "Defines the position, rotation and scale of a Game Object along the X, Y and Z axis.";

    /// <summary>
    ///     Gets the property descriptor for the Position property.
    /// </summary>
    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    /// <summary>
    ///     Gets the property descriptor for the Rotation property.
    /// </summary>
    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    /// <summary>
    ///     Gets the property descriptor for the Scale property.
    /// </summary>
    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        // Remember selection for two-way change propagation
        this.selectedItems = items;

        this.LogUpdateValues(items.Count);

        this.UpdatePositionValues(items);
        this.UpdateRotationValues(items);
        this.UpdateScaleValues(items);
    }

    // When the ViewModel property changes as a result of user interaction in the VectorBox,
    // propagate that change to the selected SceneNode transform components.
    partial void OnPositionXChanged(float value)
    {
        if (this.isApplyingEditorChanges)
        {
            return;
        }

        if (this.selectedItems is null)
        {
            return;
        }

        this.LogApplyingChange("PositionX", value, this.selectedItems.Count);

        try
        {
            this.isApplyingEditorChanges = true;

            // capture old snapshots for undo/redo
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null)
                {
                    continue;
                }

                var p = transform.LocalPosition;
                p.X = value;
                transform.LocalPosition = p;
            }

            // notify message with new snapshots
            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "PositionX")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("PositionX", ex);
            throw;
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }
    }

    partial void OnPositionYChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null)
        {
            return;
        }

        this.LogApplyingChange("PositionY", value, this.selectedItems.Count);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var p = transform.LocalPosition;
                p.Y = value;
                transform.LocalPosition = p;
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "PositionY")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("PositionY", ex);
            throw;
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }
    }

    partial void OnPositionZChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null)
        {
            return;
        }

        this.LogApplyingChange("PositionZ", value, this.selectedItems.Count);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var p = transform.LocalPosition;
                p.Z = value;
                transform.LocalPosition = p;
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "PositionZ")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("PositionZ", ex);
            throw;
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }
    }

    partial void OnRotationXChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("RotationX", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var q = transform.LocalRotation;
                var euler = TransformConverter.QuaternionToEulerDegrees(q);
                euler.X = value;
                transform.LocalRotation = TransformConverter.EulerDegreesToQuaternion(euler);
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "RotationX")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("RotationX", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    partial void OnRotationYChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("RotationY", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var q = transform.LocalRotation;
                var euler = TransformConverter.QuaternionToEulerDegrees(q);
                euler.Y = value;
                transform.LocalRotation = TransformConverter.EulerDegreesToQuaternion(euler);
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "RotationY")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("RotationY", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    partial void OnRotationZChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("RotationZ", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();

            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var q = transform.LocalRotation;
                var euler = TransformConverter.QuaternionToEulerDegrees(q);
                euler.Z = value;
                transform.LocalRotation = TransformConverter.EulerDegreesToQuaternion(euler);
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "RotationZ")));
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("RotationZ", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    partial void OnScaleXChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("ScaleX", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();
            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var s = transform.LocalScale;

                // Apply the user-entered value directly — the control validates inputs.
                s.X = value;
                transform.LocalScale = s;
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "ScaleX")));

            // Reflect the raw user value back to the ViewModel — do not normalize here.
            this.ScaleX = value;
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("ScaleX", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    partial void OnScaleYChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("ScaleY", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();
            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var s = transform.LocalScale;

                // Apply the user-entered value directly — the control validates inputs.
                s.Y = value;
                transform.LocalScale = s;
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "ScaleY")));

            // Reflect the raw user value back to the ViewModel — do not normalize here.
            this.ScaleY = value;
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("ScaleY", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    partial void OnScaleZChanged(float value)
    {
        if (this.isApplyingEditorChanges || this.selectedItems is null) return;

        var targetCount = this.selectedItems.Count;
        this.LogApplyingChange("ScaleZ", value, targetCount);

        try
        {
            this.isApplyingEditorChanges = true;
            var nodes = this.selectedItems.ToList();
            var oldSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t.LocalPosition, t.LocalRotation, t.LocalScale))
                .ToList();
            foreach (var item in nodes)
            {
                var transform = item.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
                if (transform is null) continue;
                var s = transform.LocalScale;

                // Apply the user-entered value directly — the control validates inputs.
                s.Z = value;
                transform.LocalScale = s;
            }

            var newSnapshots = nodes.Select(n => n.Components.OfType<TransformComponent>().FirstOrDefault())
                .Select(t => t is null ? default(TransformSnapshot) : new TransformSnapshot(t!.LocalPosition, t!.LocalRotation, t!.LocalScale))
                .ToList();

            _ = (this.messengerSvc?.Send(new SceneNodeTransformAppliedMessage(nodes, oldSnapshots, newSnapshots, "ScaleZ")));

            // Reflect the raw user value back to the ViewModel — do not normalize here.
            this.ScaleZ = value;
        }
        catch (Exception ex)
        {
            this.LogApplyFailed("ScaleZ", ex);
            throw;
        }
        finally { this.isApplyingEditorChanges = false; }
    }

    private void UpdatePositionValues(ICollection<SceneNode> items)
    {
        var mixedPosX = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
            return transform?.LocalPosition.X ?? 0;
        });

        if (mixedPosX.HasValue)
        {
            this.PositionX = mixedPosX.Value;
            this.PositionXIsIndeterminate = false;
        }
        else
        {
            this.PositionXIsIndeterminate = true;
            this.PositionX = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent)?.LocalPosition.X ?? 0
                : 0;
        }

        var mixedPosY = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
            return transform?.LocalPosition.Y ?? 0;
        });

        if (mixedPosY.HasValue)
        {
            this.PositionY = mixedPosY.Value;
            this.PositionYIsIndeterminate = false;
        }
        else
        {
            this.PositionYIsIndeterminate = true;
            this.PositionY = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent)?.LocalPosition.Y ?? 0
                : 0;
        }

        var mixedPosZ = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent;
            return transform?.LocalPosition.Z ?? 0;
        });

        if (mixedPosZ.HasValue)
        {
            this.PositionZ = mixedPosZ.Value;
            this.PositionZIsIndeterminate = false;
        }
        else
        {
            this.PositionZIsIndeterminate = true;
            this.PositionZ = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent)?.LocalPosition.Z ?? 0
                : 0;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0019:Use pattern matching", Justification = "code clarity")]
    private void UpdateRotationValues(ICollection<SceneNode> items)
    {
        var mixedRotX = MixedValues.GetMixedValue(items, static e =>
        {
            var transform = e.Components.FirstOrDefault(static c => c is TransformComponent) as TransformComponent;
            return transform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation).X;
        });

        if (mixedRotX.HasValue)
        {
            this.RotationX = mixedRotX.Value;
            this.RotationXIsIndeterminate = false;
        }
        else
        {
            this.RotationXIsIndeterminate = true;
            var firstTransform = items.FirstOrDefault() is { } first ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent) : null;
            this.RotationX = firstTransform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(firstTransform.LocalRotation).X;
        }

        var mixedRotY = MixedValues.GetMixedValue(items, static e =>
        {
            var transform = e.Components.FirstOrDefault(static c => c is TransformComponent) as TransformComponent;
            return transform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation).Y;
        });

        if (mixedRotY.HasValue)
        {
            this.RotationY = mixedRotY.Value;
            this.RotationYIsIndeterminate = false;
        }
        else
        {
            this.RotationYIsIndeterminate = true;
            var firstTransform = items.FirstOrDefault() is { } first ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent) : null;
            this.RotationY = firstTransform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(firstTransform.LocalRotation).Y;
        }

        var mixedRotZ = MixedValues.GetMixedValue(items, static e =>
        {
            var transform = e.Components.FirstOrDefault(static c => c is TransformComponent) as TransformComponent;
            return transform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation).Z;
        });

        if (mixedRotZ.HasValue)
        {
            this.RotationZ = mixedRotZ.Value;
            this.RotationZIsIndeterminate = false;
        }
        else
        {
            this.RotationZIsIndeterminate = true;
            var firstTransform = items.FirstOrDefault() is { } first ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent) : null;
            this.RotationZ = firstTransform is null ? 0 : TransformConverter.QuaternionToEulerDegrees(firstTransform.LocalRotation).Z;
        }
    }

    private void UpdateScaleValues(ICollection<SceneNode> items)
    {
        // Local helper: retrieve a (possibly mixed) scale component value for the collection.
        static float? GetMixedScale(ICollection<SceneNode> nodes, Func<TransformComponent, float> selector)
            => MixedValues.GetMixedValue(nodes, e => e.Components.FirstOrDefault(c => c is TransformComponent) is not TransformComponent transform ? 0 : selector(transform));

        // Local helper: handle one axis (X/Y/Z) — use delegates to extract/replace the component within the scale Vector3,
        // and to write back to the ViewModel properties.
        static void ProcessAxis(
            ICollection<SceneNode> nodes,
            Func<System.Numerics.Vector3, float> extract,
            Func<System.Numerics.Vector3, float, System.Numerics.Vector3> setComponent,
            Action<float> setViewModelScale,
            Action<bool> setIsIndeterminate)
        {
            var mixed = GetMixedScale(nodes, t => extract(t.LocalScale));
            if (mixed.HasValue)
            {
                setViewModelScale(mixed.Value);
                setIsIndeterminate(false);
                return;
            }

            setIsIndeterminate(true);

            var firstTransform = nodes.FirstOrDefault() is { } first ? (first.Components.FirstOrDefault(c => c is TransformComponent) as TransformComponent) : null;
            if (firstTransform is null)
            {
                setViewModelScale(0);
                return;
            }

            var s = firstTransform.LocalScale;
            var comp = extract(s);
            var norm = TransformConverter.NormalizeScaleValue(comp);
            if (norm != comp)
            {
                s = setComponent(s, norm);
                firstTransform.LocalScale = s; // update model to canonical non-zero value
            }

            setViewModelScale(extract(s));
        }

        // Process X axis
        ProcessAxis(
            items,
            v => v.X,
            (v, val) =>
            {
                v.X = val;
                return v;
            },
            value => this.ScaleX = value,
            indet => this.ScaleXIsIndeterminate = indet);

        // Process Y axis
        ProcessAxis(
            items,
            v => v.Y,
            (v, val) =>
            {
                v.Y = val;
                return v;
            },
            value => this.ScaleY = value,
            indet => this.ScaleYIsIndeterminate = indet);

        // Process Z axis
        ProcessAxis(
            items,
            v => v.Z,
            (v, val) =>
            {
                v.Z = val;
                return v;
            },
            value => this.ScaleZ = value,
            indet => this.ScaleZIsIndeterminate = indet);
    }
}
