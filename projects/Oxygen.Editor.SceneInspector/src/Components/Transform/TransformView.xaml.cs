// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Transform;

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
        => this.ValidateAxis(TransformEditFieldGroup.Position, e);

    private void RotationBox_Validate(object? sender, ValidationEventArgs<double> e)
        => this.ValidateAxis(TransformEditFieldGroup.Rotation, e);

    private void ScaleBox_Validate(object? sender, ValidationEventArgs<double> e)
        => this.ValidateAxis(TransformEditFieldGroup.Scale, e);

    // The vector box names the component it validates; the schema range of that axis decides.
    private void ValidateAxis(TransformEditFieldGroup group, ValidationEventArgs<double> e)
    {
        e.IsValid = e.Target is Component component
            && TransformEditController.CheckValue(group, component, e.NewValue) == RangeCheck.InRange;
        this.ViewModel?.ReportControlValidation(group, e);
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
