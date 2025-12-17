// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///     A styled card control for displaying a property and its value in a property editor UI.
///     Supports visual states for normal, pointer-over, and disabled interactions.
/// </summary>
[TemplateVisualState(Name = NormalState, GroupName = CommonStates)]
[TemplateVisualState(Name = PointerOverState, GroupName = CommonStates)]
[TemplateVisualState(Name = DisabledState, GroupName = CommonStates)]
public partial class PropertyCard : ContentControl
{
    /// <summary>The name of the visual state group for common states.</summary>
    public const string CommonStates = "CommonStates";

    /// <summary>The visual state name for the normal state.</summary>
    public const string NormalState = "Normal";

    /// <summary>The visual state name for the pointer-over state.</summary>
    public const string PointerOverState = "PointerOver";

    /// <summary>The visual state name for the disabled state.</summary>
    public const string DisabledState = "Disabled";

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="PropertyName"/> property.
    /// </summary>
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

    /// <summary>
    /// Gets or sets the name of the property displayed by this card.
    /// </summary>
    public string PropertyName
    {
        get => (string)this.GetValue(PropertyNameProperty);
        set => this.SetValue(PropertyNameProperty, value);
    }

    /// <summary>
    /// Applies the control template and wires up visual state event handlers.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.IsEnabledChanged -= this.OnIsEnabledChanged;
        this.CheckInitialVisualState();

        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.IsEnabledChanged += this.OnIsEnabledChanged;
    }

    /// <summary>
    /// Checks and sets the initial visual state based on the enabled state.
    /// </summary>
    private void CheckInitialVisualState()
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    /// <summary>
    /// Handles changes to the enabled state and updates the visual state accordingly.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    /// <summary>
    /// Handles pointer entering the control and updates the visual state to pointer-over.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnPointerEntered(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "PointerOver", useTransitions: true);

    /// <summary>
    /// Handles pointer exiting the control and updates the visual state to normal.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnPointerExited(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "Normal", useTransitions: true);
}
