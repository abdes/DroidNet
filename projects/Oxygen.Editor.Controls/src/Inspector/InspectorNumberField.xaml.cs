// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls;

/// <summary>Composes a native numeric caption/editor and its field feedback.</summary>
public sealed partial class InspectorNumberField : UserControl
{
    /// <summary>Identifies the field caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the value qualifier.</summary>
    public static readonly DependencyProperty QualifierProperty = Register<string>(nameof(Qualifier), string.Empty);

    /// <summary>Identifies the value prefix.</summary>
    public static readonly DependencyProperty PrefixProperty = Register<string>(nameof(Prefix), string.Empty);

    /// <summary>Identifies the numeric value.</summary>
    public static readonly DependencyProperty NumberValueProperty = Register<float>(nameof(NumberValue), 0f);

    /// <summary>Identifies mixed-value presentation.</summary>
    public static readonly DependencyProperty IsMixedProperty = Register<bool>(nameof(IsMixed), false);

    /// <summary>Identifies the existing NumberBox mask.</summary>
    public static readonly DependencyProperty MaskProperty = Register<string>(nameof(Mask), "~.###");

    /// <summary>Identifies the property-row layout.</summary>
    public static readonly DependencyProperty LayoutProperty = Register<PropertyLayout>(nameof(Layout), PropertyLayout.Auto);

    /// <summary>Identifies the shared caption width.</summary>
    public static readonly DependencyProperty LabelWidthProperty = Register<double>(nameof(LabelWidth), 124d);

    /// <summary>Identifies the minimum usable numeric editor width.</summary>
    public static readonly DependencyProperty EditorMinimumWidthProperty = Register<double>(nameof(EditorMinimumWidth), 128d);

    /// <summary>Identifies current validation text.</summary>
    public static readonly DependencyProperty ErrorTextProperty = Register<string>(nameof(ErrorText), string.Empty);

    /// <summary>Identifies whether validation text is shown.</summary>
    public static readonly DependencyProperty HasErrorProperty = DependencyProperty.Register(
        nameof(HasError), typeof(bool), typeof(InspectorNumberField), new PropertyMetadata(false, OnFeedbackChanged));

    /// <summary>Identifies current stored-value applicability text.</summary>
    public static readonly DependencyProperty ApplicabilityTextProperty = DependencyProperty.Register(
        nameof(ApplicabilityText), typeof(string), typeof(InspectorNumberField), new PropertyMetadata(string.Empty, OnFeedbackChanged));

    /// <summary>Identifies the consumer's scoped property-card style.</summary>
    public static readonly DependencyProperty CardStyleProperty = Register<Style>(nameof(CardStyle), null);

    /// <summary>Identifies the consumer's scoped numeric style.</summary>
    public static readonly DependencyProperty NumberStyleProperty = Register<Style>(nameof(NumberStyle), null);

    /// <summary>Initializes a new instance of the <see cref="InspectorNumberField"/> class.</summary>
    public InspectorNumberField()
    {
        this.InitializeComponent();
    }

    /// <summary>Occurs when the native NumberBox begins an edit.</summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>Occurs when the native NumberBox commits or cancels an edit.</summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>Forwards the original validation event, including raw expression text.</summary>
    public event EventHandler<ValidationEventArgs<float>>? Validate;

    /// <summary>Gets or sets the native drag caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the numeric unit/annotation.</summary>
    public string Qualifier { get => (string)this.GetValue(QualifierProperty); set => this.SetValue(QualifierProperty, value); }

    /// <summary>Gets or sets the prefix annotation.</summary>
    public string Prefix { get => (string)this.GetValue(PrefixProperty); set => this.SetValue(PrefixProperty, value); }

    /// <summary>Gets or sets the numeric value.</summary>
    public float NumberValue { get => (float)this.GetValue(NumberValueProperty); set => this.SetValue(NumberValueProperty, value); }

    /// <summary>Gets or sets a value indicating whether the field has mixed values.</summary>
    public bool IsMixed { get => (bool)this.GetValue(IsMixedProperty); set => this.SetValue(IsMixedProperty, value); }

    /// <summary>Gets or sets the native formatting mask.</summary>
    public string Mask { get => (string)this.GetValue(MaskProperty); set => this.SetValue(MaskProperty, value); }

    /// <summary>Gets or sets the property-row layout policy.</summary>
    public PropertyLayout Layout { get => (PropertyLayout)this.GetValue(LayoutProperty); set => this.SetValue(LayoutProperty, value); }

    /// <summary>Gets or sets the shared label-column width.</summary>
    public double LabelWidth { get => (double)this.GetValue(LabelWidthProperty); set => this.SetValue(LabelWidthProperty, value); }

    /// <summary>Gets or sets minimum usable editor width.</summary>
    public double EditorMinimumWidth { get => (double)this.GetValue(EditorMinimumWidthProperty); set => this.SetValue(EditorMinimumWidthProperty, value); }

    /// <summary>Gets or sets current field validation text.</summary>
    public string ErrorText { get => (string)this.GetValue(ErrorTextProperty); set => this.SetValue(ErrorTextProperty, value); }

    /// <summary>Gets or sets a value indicating whether field validation text is visible.</summary>
    public bool HasError { get => (bool)this.GetValue(HasErrorProperty); set => this.SetValue(HasErrorProperty, value); }

    /// <summary>Gets or sets applicability text, independently of errors.</summary>
    public string ApplicabilityText { get => (string)this.GetValue(ApplicabilityTextProperty); set => this.SetValue(ApplicabilityTextProperty, value); }

    /// <summary>Gets or sets the consumer's existing card style.</summary>
    public Style? CardStyle { get => (Style?)this.GetValue(CardStyleProperty); set => this.SetValue(CardStyleProperty, value); }

    /// <summary>Gets or sets the consumer's existing numeric style.</summary>
    public Style? NumberStyle { get => (Style?)this.GetValue(NumberStyleProperty); set => this.SetValue(NumberStyleProperty, value); }

    private Visibility ErrorVisibility => this.HasError ? Visibility.Visible : Visibility.Collapsed;

    private Visibility ApplicabilityVisibility => string.IsNullOrEmpty(this.ApplicabilityText) ? Visibility.Collapsed : Visibility.Visible;

    private static DependencyProperty Register<T>(string name, object? defaultValue)
        => DependencyProperty.Register(name, typeof(T), typeof(InspectorNumberField), new PropertyMetadata(defaultValue));

    private static void OnFeedbackChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorNumberField)sender).Bindings.Update();

    private void OnEditSessionStarted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, args);

    private void OnEditSessionCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, args);

    private void OnValidate(object? sender, ValidationEventArgs<float> args)
        => this.Validate?.Invoke(this, args);
}
