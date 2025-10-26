// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class TransformViewModel : ComponentPropertyEditor
{
    /// <summary>
    ///     Gets or sets the X position of the game object in the scene. The position can be abosulte
    ///     (relative to the scene) or relative to the parent object.
    /// </summary>
    [ObservableProperty]
    public partial float? PositionX { get; set; }

    [ObservableProperty]
    public partial float? PositionY { get; set; }

    [ObservableProperty]
    public partial float? PositionZ { get; set; }

    [ObservableProperty]
    public partial float? RotationX { get; set; }

    [ObservableProperty]
    public partial float? RotationY { get; set; }

    [ObservableProperty]
    public partial float? RotationZ { get; set; }

    [ObservableProperty]
    public partial float? ScaleX { get; set; }

    [ObservableProperty]
    public partial float? ScaleY { get; set; }

    [ObservableProperty]
    public partial float? ScaleZ { get; set; }

    /// <inheritdoc />
    public override string Header => "Transform";

    /// <inheritdoc />
    public override string Description =>
        "Defines the position, rotation and scale of a Game Object along the X, Y and Z axis.";

    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
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
