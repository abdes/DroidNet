// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
/// ViewModel for editing the transform properties (position, rotation, scale) of selected SceneNode instances.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class TransformViewModel(ILoggerFactory? loggerFactory = null) : ComponentPropertyEditor
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<TransformViewModel>();

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

            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null)
                {
                    continue;
                }

                var p = transform.Position;
                p.X = value;
                transform.Position = p;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var p = transform.Position;
                p.Y = value;
                transform.Position = p;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var p = transform.Position;
                p.Z = value;
                transform.Position = p;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var r = transform.Rotation;
                r.X = value;
                transform.Rotation = r;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var r = transform.Rotation;
                r.Y = value;
                transform.Rotation = r;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var r = transform.Rotation;
                r.Z = value;
                transform.Rotation = r;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var s = transform.Scale;
                s.X = value;
                transform.Scale = s;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var s = transform.Scale;
                s.Y = value;
                transform.Scale = s;
            }
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
            foreach (var item in this.selectedItems)
            {
                var transform = item.Components.FirstOrDefault(c => c is Transform) as Transform;
                if (transform is null) continue;
                var s = transform.Scale;
                s.Z = value;
                transform.Scale = s;
            }
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
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Position.X ?? 0;
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
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Position.X ?? 0
                : 0;
        }

        var mixedPosY = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Position.Y ?? 0;
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
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Position.Y ?? 0
                : 0;
        }

        var mixedPosZ = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Position.Z ?? 0;
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
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Position.Z ?? 0
                : 0;
        }
    }

    private void UpdateRotationValues(ICollection<SceneNode> items)
    {
        var mixedRotX = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Rotation.X ?? 0;
        });

        if (mixedRotX.HasValue)
        {
            this.RotationX = mixedRotX.Value;
            this.RotationXIsIndeterminate = false;
        }
        else
        {
            this.RotationXIsIndeterminate = true;
            this.RotationX = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Rotation.X ?? 0
                : 0;
        }

        var mixedRotY = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Rotation.Y ?? 0;
        });

        if (mixedRotY.HasValue)
        {
            this.RotationY = mixedRotY.Value;
            this.RotationYIsIndeterminate = false;
        }
        else
        {
            this.RotationYIsIndeterminate = true;
            this.RotationY = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Rotation.Y ?? 0
                : 0;
        }

        var mixedRotZ = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Rotation.Z ?? 0;
        });

        if (mixedRotZ.HasValue)
        {
            this.RotationZ = mixedRotZ.Value;
            this.RotationZIsIndeterminate = false;
        }
        else
        {
            this.RotationZIsIndeterminate = true;
            this.RotationZ = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Rotation.Z ?? 0
                : 0;
        }
    }

    private void UpdateScaleValues(ICollection<SceneNode> items)
    {
        var mixedScaleX = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Scale.X ?? 0;
        });

        if (mixedScaleX.HasValue)
        {
            this.ScaleX = mixedScaleX.Value;
            this.ScaleXIsIndeterminate = false;
        }
        else
        {
            this.ScaleXIsIndeterminate = true;
            this.ScaleX = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Scale.X ?? 0
                : 0;
        }

        var mixedScaleY = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Scale.Y ?? 0;
        });

        if (mixedScaleY.HasValue)
        {
            this.ScaleY = mixedScaleY.Value;
            this.ScaleYIsIndeterminate = false;
        }
        else
        {
            this.ScaleYIsIndeterminate = true;
            this.ScaleY = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Scale.Y ?? 0
                : 0;
        }

        var mixedScaleZ = MixedValues.GetMixedValue(items, e =>
        {
            var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
            return transform?.Scale.Z ?? 0;
        });

        if (mixedScaleZ.HasValue)
        {
            this.ScaleZ = mixedScaleZ.Value;
            this.ScaleZIsIndeterminate = false;
        }
        else
        {
            this.ScaleZIsIndeterminate = true;
            this.ScaleZ = items.FirstOrDefault() is { } first
                ? (first.Components.FirstOrDefault(c => c is Transform) as Transform)?.Scale.Z ?? 0
                : 0;
        }
    }
}
