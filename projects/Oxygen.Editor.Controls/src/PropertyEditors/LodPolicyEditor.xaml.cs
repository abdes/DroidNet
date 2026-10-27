// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// The Level of detail editor of a geometry's LOD policy: its kind and the boundaries between its LODs, as the Scene
/// Inspector's Geometry section and a model's import defaults edit them.
/// </summary>
public sealed partial class LodPolicyEditor : UserControl
{
    /// <summary>Identifies the <see cref="Model"/> property.</summary>
    public static readonly DependencyProperty ModelProperty = DependencyProperty.Register(
        nameof(Model), typeof(LodPolicyEditorModel), typeof(LodPolicyEditor), new PropertyMetadata(defaultValue: null));

    /// <summary>Identifies the <see cref="Label"/> property.</summary>
    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label), typeof(string), typeof(LodPolicyEditor), new PropertyMetadata("Level of detail"));

    /// <summary>Initializes a new instance of the <see cref="LodPolicyEditor"/> class.</summary>
    public LodPolicyEditor()
    {
        this.InitializeComponent();
    }

    /// <summary>Gets or sets the policy's rows.</summary>
    public LodPolicyEditorModel? Model
    {
        get => (LodPolicyEditorModel?)this.GetValue(ModelProperty);
        set => this.SetValue(ModelProperty, value);
    }

    /// <summary>Gets or sets the policy row's label.</summary>
    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }
}
