// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Projects;

using static Oxygen.Editor.WorldEditor.PropertiesEditor.MultiSelectionDetails<Oxygen.Editor.Projects.GameEntity>;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class TransformViewModel : ComponentPropertyEditor
{
    /// <inheritdoc/>
    public override string Header => "Transform";

    /// <inheritdoc/>
    public override string Description => "Description of Transform properties";

    [ObservableProperty]
    private float? positionX;

    [ObservableProperty]
    private float? positionY;

    [ObservableProperty]
    private float? positionZ;

    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <inheritdoc/>
    public override void UpdateValues(IList<GameEntity> items)
    {
        this.PositionX = GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.X ?? 0;
            });

        this.PositionY = GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.Y ?? 0;
            });

        this.PositionZ = GetMixedValue(
            items,
            e =>
            {
                var transform = e.Components.FirstOrDefault(c => c is Transform) as Transform;
                return transform?.Position.Z ?? 0;
            });
    }
}
