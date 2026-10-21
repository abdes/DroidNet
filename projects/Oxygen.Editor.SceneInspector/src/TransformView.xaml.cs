// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.SceneInspector;

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
        ConfigureAxisLabels(this.PositionBox);
        ConfigureAxisLabels(this.RotationBox);
        ConfigureAxisLabels(this.ScaleBox);
    }

    private static void ConfigureAxisLabels(VectorBox vector)
    {
        vector.ComponentLabelForegrounds[Component.X] = new SolidColorBrush(Colors.Red);
        vector.ComponentLabelForegrounds[Component.Y] = new SolidColorBrush(Colors.Green);
        vector.ComponentLabelForegrounds[Component.Z] = new SolidColorBrush(Colors.Blue);
    }

    private void PositionBox_Validate(object? sender, ValidationEventArgs<double> e)
    {
        e.IsValid = double.IsFinite(e.NewValue);
        this.ViewModel?.ReportControlValidation(TransformEditFieldGroup.Position, e);
    }

    private void RotationBox_Validate(object? sender, ValidationEventArgs<double> e)
    {
        var v = e.NewValue;

        // Reject non-finite values
        if (!double.IsFinite(v))
        {
            e.IsValid = false;
            this.ViewModel?.ReportControlValidation(TransformEditFieldGroup.Rotation, e);
            return;
        }

        // Enforce editor-expected range: rotation must be between -180 and 180 degrees (inclusive)
        const double min = -180;
        const double max = 180;
        e.IsValid = v is >= min and <= max;
        this.ViewModel?.ReportControlValidation(TransformEditFieldGroup.Rotation, e);
    }

    private void ScaleBox_Validate(object? sender, ValidationEventArgs<double> e)
    {
        var v = e.NewValue;

        // Reject non-finite values
        if (!double.IsFinite(v))
        {
            e.IsValid = false;
            this.ViewModel?.ReportControlValidation(TransformEditFieldGroup.Scale, e);
            return;
        }

        // Disallow near-zero scale which degenerates geometry; allow negatives.
        const double minMagnitude = 1e-3; // tweakable threshold
        e.IsValid = Math.Abs(v) >= minMagnitude;
        this.ViewModel?.ReportControlValidation(TransformEditFieldGroup.Scale, e);
    }

    private void PositionBox_EditSessionStarted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.BeginEditSession(TransformEditFieldGroup.Position, e);

    private void PositionBox_EditSessionCompleted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.CompleteEditSession(TransformEditFieldGroup.Position, e);

    private void RotationBox_EditSessionStarted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.BeginEditSession(TransformEditFieldGroup.Rotation, e);

    private void RotationBox_EditSessionCompleted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.CompleteEditSession(TransformEditFieldGroup.Rotation, e);

    private void ScaleBox_EditSessionStarted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.BeginEditSession(TransformEditFieldGroup.Scale, e);

    private void ScaleBox_EditSessionCompleted(object? sender, VectorBoxEditSessionEventArgs e)
        => this.ViewModel?.CompleteEditSession(TransformEditFieldGroup.Scale, e);
}
