// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class TransformViewModel : ComponentPropertyEditor
{
    /// <summary>
    /// Gets or sets the X position of the game object in the scene. The position can be abosulte
    /// (relative to the scene) or relative to the parent object.
    /// </summary>
    [ObservableProperty]
    private float? positionX;

    [ObservableProperty]
    private float? positionY;

    [ObservableProperty]
    private float? positionZ;

    [ObservableProperty]
    private float? rotationX;

    [ObservableProperty]
    private float? rotationY;

    [ObservableProperty]
    private float? rotationZ;

    [ObservableProperty]
    private float? scaleX;

    [ObservableProperty]
    private float? scaleY;

    [ObservableProperty]
    private float? scaleZ;

    /// <inheritdoc/>
    public override string Header => "Transform";

    /// <inheritdoc/>
    public override string Description => "Defines the position, rotation and scale of a Game Object along the X, Y and Z axis.";

    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <inheritdoc/>
    public override void UpdateValues(ICollection<GameEntity> items)
    {
        this.PositionX = MixedValues.GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.X ?? 0;
            });

        this.PositionY = MixedValues.GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.Y ?? 0;
            });

        this.PositionZ = MixedValues.GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.Z ?? 0;
            });
    }
}
