// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

[TemplateVisualState(Name = NormalState, GroupName = CommonStates)]
[TemplateVisualState(Name = PointerOverState, GroupName = CommonStates)]
[TemplateVisualState(Name = DisabledState, GroupName = CommonStates)]
public class PropertyCard : ContentControl
{
    private const string CommonStates = "CommonStates";
    private const string NormalState = "Normal";
    private const string PointerOverState = "PointerOver";
    private const string DisabledState = "Disabled";

    public static readonly DependencyProperty PropertyNameProperty =
        DependencyProperty.Register(
            nameof(PropertyName),
            typeof(string),
            typeof(PropertyCard),
            new PropertyMetadata(default(string)));

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyCard"/> class.
    /// </summary>
    public PropertyCard()
    {
        this.DefaultStyleKey = typeof(PropertyCard);
    }

    public string PropertyName
    {
        get => (string)this.GetValue(PropertyNameProperty);
        set => this.SetValue(PropertyNameProperty, value);
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.IsEnabledChanged -= this.OnIsEnabledChanged;
        this.CheckInitialVisualState();

        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.IsEnabledChanged += this.OnIsEnabledChanged;
    }

    private void CheckInitialVisualState()
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    private void OnPointerEntered(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "PointerOver", useTransitions: true);

    private void OnPointerExited(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "Normal", useTransitions: true);
}
