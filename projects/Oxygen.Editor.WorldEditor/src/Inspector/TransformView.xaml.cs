// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Represents the view for editing transform properties in the World Editor.
/// </summary>
[ViewModel(typeof(TransformViewModel))]
public partial class TransformView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TransformView"/> class.
    /// </summary>
    public TransformView()
    {
        this.InitializeComponent();
    }

    private void RotationBox_Validate(object? sender, ValidationEventArgs<float> e)
    {
        var v = e.NewValue;

        // Reject non-finite values
        if (float.IsNaN(v) || float.IsInfinity(v))
        {
            e.IsValid = false;
            return;
        }

        // Enforce editor-expected range: rotation must be between -180 and 180 degrees (inclusive)
        const float min = -180f;
        const float max = 180f;
        e.IsValid = v is >= min and <= max;
    }

    private void ScaleBox_Validate(object? sender, ValidationEventArgs<float> e)
    {
        var v = e.NewValue;

        // Reject non-finite values
        if (float.IsNaN(v) || float.IsInfinity(v))
        {
            e.IsValid = false;
            return;
        }

        // Disallow near-zero scale which degenerates geometry; allow negatives.
        const float minMagnitude = 1e-3f; // tweakable threshold
        e.IsValid = MathF.Abs(v) >= minMagnitude;
    }
}
