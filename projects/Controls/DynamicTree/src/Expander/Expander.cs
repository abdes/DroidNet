// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control which has two states expanded or collapsed which can be toggled by the user
///     when the control is tapped or double tapped.
/// </summary>
/// <remarks>
///     The control is always in the state specified by the value of the <see cref="IsExpanded" />
///     property. When the control is <see cref="UIElement.Tapped">tapped</see> or
///     <see
///         cref="UIElement.DoubleTapped">
///         double tapped
///     </see>
///     it fires the <see cref="Expand" /> or
///     <see cref="Collapse" /> event depending on whether it is currently in the expanded or the
///     collapsed state.
///     <para>
///         The event handler of the <see cref="Expand" /> and <see cref="Collapse" /> events can then
///         decide to do the corresponding action (or not) and set the <see cref="IsExpanded" /> property of
///         the <see cref="Expander" /> control accordingly, which will in turn put the control in the
///         corresponding visual state. This effectively makes <see cref="IsExpanded" /> property a visual
///         state only property that does not trigger the expand/collapse action when changed.
///     </para>
/// </remarks>
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplatePart(Name = ActiveElement, Type = typeof(UIElement))]
public partial class Expander : Control
{
    /// <summary>
    /// The name of the VisualStateGroup that contains the expand/collapse states.
    /// </summary>
    public const string ExpansionVisualStates = "ExpansionStates";

    /// <summary>
    /// Visual state name used when the control is expanded.
    /// </summary>
    public const string ExpandedVisualState = "Expanded";

    /// <summary>
    /// Visual state name used when the control is collapsed.
    /// </summary>
    public const string CollapsedVisualState = "Collapsed";

    /// <summary>
    /// The name of the active element part used to handle input events.
    /// </summary>
    public const string ActiveElement = "PART_ActiveElement";

    private ILogger? logger;

    private UIElement? activeElement;

    /// <summary>
    ///     Initializes a new instance of the <see cref="Expander" /> class.
    /// </summary>
    public Expander()
    {
        this.DefaultStyleKey = typeof(Expander);
    }

    /// <summary>
    ///     Toggles the expanded or collapsed state of the <see cref="Expander" /> control.
    /// </summary>
    /// <remarks>
    ///     <para>
    ///         This method switches the state of the <see cref="Expander" /> control between expanded and
    ///         collapsed. If the control is currently collapsed, it will invoke the <see cref="Expand" />
    ///         event and set the <see cref="IsExpanded" /> property to <see langword="true" />. If the
    ///         control is currently expanded, it will invoke the <see cref="Collapse" /> event and set the
    ///         <see cref="IsExpanded" /> property to <see langword="false" />.
    ///     </para>
    ///     <para>
    ///         <strong>Note:</strong> The actual state change is handled by the event handlers for the
    ///         <see cref="Expand" /> and <see cref="Collapse" /> events. These handlers should update the
    ///         <see cref="IsExpanded" /> property accordingly to reflect the new state.
    ///     </para>
    /// </remarks>
    /// <example>
    ///     <para>
    ///         <strong>Example Usage:</strong>
    ///     </para>
    ///     <![CDATA[
    /// var expander = new Expander();
    /// expander.Expand += (s, e) => expander.IsExpanded = true;
    /// expander.Collapse += (s, e) => expander.IsExpanded = false;
    /// expander.Toggle(); // Toggles the state between expanded and collapsed
    /// ]]>
    /// </example>
    public void Toggle()
    {
        this.LogToggle();

        if (!this.IsExpanded)
        {
            this.Expand?.Invoke(this, EventArgs.Empty);
        }
        else
        {
            this.Collapse?.Invoke(this, EventArgs.Empty);
        }
    }

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

    private void UpdateVisualState()
        => _ = VisualStateManager.GoToState(
            this,
            this.IsExpanded ? ExpandedVisualState : CollapsedVisualState,
            useTransitions: true);
}
