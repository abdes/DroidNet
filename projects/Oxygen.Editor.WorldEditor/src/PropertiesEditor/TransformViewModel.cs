// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public class TransformViewModel : IPropertiesViewModel
{
    /// <inheritdoc/>
    public string Header => "Transform";

    /// <inheritdoc/>
    public string Description => "Description of Transform properties";

    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };
}
