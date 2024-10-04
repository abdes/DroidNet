// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

/// <summary>
/// Represents a control which has two states expanded or collapsed which can be toggled by the user when the control is tapped or
/// double tapped.
/// </summary>
/// <remarks>
/// The control is always in the state specified by the value of the <see cref="IsExpanded" /> property. When the control is
/// <see cref="UIElement.Tapped">tapped</see> or <see cref="UIElement.DoubleTapped">double tapped</see> it fires the
/// <see cref="Expand" /> and <see cref="Collapse" /> event depending on whether it is currently in the expanded or the collapsed
/// state.
/// <para>
/// The event handler of the <see cref="Expand" /> and <see cref="Collapse" /> events can then decide to do the corresponding action
/// (or not) and set the <see cref="IsExpanded" /> property of the <see cref="Expander" /> control accordingly, whihc will in
/// turn put the control in the corresponding visual state.
/// </para>
/// </remarks>
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplatePart(Name = ActiveElement, Type = typeof(UIElement))]
public partial class Expander : Control
{
    private const string ExpansionVisualStates = "ExpansionStates";
    private const string ExpandedVisualState = "Expanded";
    private const string CollapsedVisualState = "Collapsed";

    private const string ActiveElement = "PART_ActiveElement";

    private UIElement? activeElement;

    public Expander() => this.DefaultStyleKey = typeof(Expander);

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.activeElement is not null)
        {
            this.activeElement.Tapped -= this.OnTapped;
            this.activeElement.DoubleTapped -= this.OnDoubleTapped;
        }

        this.activeElement = this.GetTemplateChild(ActiveElement) as UIElement;

        if (this.activeElement is not null)
        {
            this.activeElement.Tapped += this.OnTapped;
            this.activeElement.DoubleTapped += this.OnDoubleTapped;
        }

        this.UpdateVisualState();
    }

    private void OnTapped(object sender, TappedRoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.Toggle();
    }

    private void OnDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.Toggle();
    }

    private void Toggle()
    {
        if (!this.IsExpanded)
        {
            this.Expand?.Invoke(this, EventArgs.Empty);
        }
        else
        {
            this.Collapse?.Invoke(this, EventArgs.Empty);
        }
    }

    private void UpdateVisualState()
        => _ = VisualStateManager.GoToState(
            this,
            this.IsExpanded ? ExpandedVisualState : CollapsedVisualState,
            useTransitions: true);
}
