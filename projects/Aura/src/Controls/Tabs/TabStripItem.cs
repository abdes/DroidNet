// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents the visual container for a single logical tab displayed in a <c>TabStrip</c>.
/// </summary>
/// <remarks>
///     <c>TabStripItem</c> is a lightweight <see cref="ContentControl"/> that renders a single
///     <see cref="TabItem"/> data model inside a templatable control. It is intended to be used
///     as the item container within a <c>TabStrip</c> or an <c>ItemsRepeater</c>-based tab
///     strip. The control exposes an <see cref="Item"/> property which should be set to the
///     corresponding <see cref="TabItem"/> instance. The template provides an area for an
///     optional icon, header text, a pinned indicator and an overlayed toolbar with pin/close
///     buttons.
///     <para>
///     The control relies on template parts and visual states to implement its behaviour.
///     Template part names are declared via <see cref="TemplatePartAttribute"/> on the
///     class; the available parts include (names shown as literal strings):
///     </para>
///     <list type="bullet">
///         <item><description><c>"PartRootGrid"</c> — root element used for visual state management.</description></item>
///         <item><description><c>"PartContentRootGrid"</c> — measured content grid containing icon, header and indicator.</description></item>
///         <item><description><c>"PartIcon"</c> — optional icon element bound to <c>TabItem.Icon</c>.</description></item>
///         <item><description><c>"PartHeader"</c> — text element bound to <c>TabItem.Header</c>.</description></item>
///         <item><description><c>"ButtonsContainer"</c> — container for the tool buttons.</description></item>
///         <item><description><c>"PartPinButton"</c> and <c>"PartCloseButton"</c> — the pin and close buttons.</description></item>
///         <item><description><c>"PartPinnedIndicator"</c> — visual indicator shown when the item is pinned.</description></item>
///     </list>
///     <para>
///     Visual states are grouped to represent common pointer/selection states and overlay
///     visibility. Key visual state groups include <c>CommonStates</c>, <c>PinStates</c> and
///     <c>OverlayStates</c>, and states such as <c>Normal</c>, <c>PointerOver</c>,
///     <c>Selected</c>, <c>Pinned</c> and <c>OverlayVisible</c> are used by the control
///     template to animate appearance changes.
///     </para>
///     <para>
///     The control exposes the following important dependency properties:
///     </para>
///     <list type="bullet">
///         <item><description><see cref="Item"/> — the <see cref="TabItem"/> data model.</description></item>
///         <item><description><see cref="IsCompact"/> — when <see langword="true"/>, tool buttons are overlayed rather than occupying layout space.</description></item>
///         <item><description><see cref="LoggerFactory"/> — optional factory used to create an <c>ILogger</c> for diagnostics.</description></item>
///     </list>
///     <para>
///     The control raises the <see cref="CloseRequested"/> event when the user requests that a tab
///     be closed (for example, by clicking the close button). Templates should bind the pin/close
///     button commands or wire up the <c>Click</c> handlers to the provided template parts. The
///     control will also toggle the pinned indicator when the pin button is used.
///     </para>
///     <para>
///     Lifetime and threading: like all XAML UI elements, <c>TabStripItem</c> must be accessed on
///     the UI thread. The control detaches event handlers and cleans up disposable proxies during
///     the <c>Unloaded</c> handler to avoid leaking resources from recycled containers.
///     </para>
/// </remarks>
/// <example>
///     <code language="xml"><![CDATA[
///     <controls:TabStrip>
///       <controls:TabStripItem Item="{Binding MyTabItem}" />
///     </controls:TabStrip>
///     ]]></code>
/// </example>
/// <seealso cref="TabItem"/>
/// <seealso cref="CloseRequested"/>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = ContentRootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = IconPartName, Type = typeof(IconSourceElement))]
[TemplatePart(Name = HeaderPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ButtonsContainerPartName, Type = typeof(StackPanel))]
[TemplatePart(Name = PinButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = CloseButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = PinnedIndicatorPartName, Type = typeof(UIElement))]
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = PointerOverVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedPointerOverVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = UnpinnedVisualState, GroupName = PinVisualStates)]
[TemplateVisualState(Name = PinnedVisualState, GroupName = PinVisualStates)]
[TemplateVisualState(Name = OverlayHiddenVisualState, GroupName = OverlayVisualStates)]
[TemplateVisualState(Name = OverlayVisibleVisualState, GroupName = OverlayVisualStates)]
[TemplateVisualState(Name = NotDraggingVisualState, GroupName = DraggingVisualStates)]
[TemplateVisualState(Name = DraggingVisualState, GroupName = DraggingVisualStates)]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]

public partial class TabStripItem : ContentControl
{
    /// <summary>The name of the root grid template part used for visual state management.</summary>
    public const string RootGridPartName = "PartRootGrid";

    /// <summary>The name of the root grid template part.</summary>
    public const string ContentRootGridPartName = "PartContentRootGrid";

    /// <summary>The name of the icon template part.</summary>
    public const string IconPartName = "PartIcon";

    /// <summary>The name of the header template part.</summary>
    public const string HeaderPartName = "PartHeader";

    /// <summary>The name of the buttons container template part.</summary>
    public const string ButtonsContainerPartName = "ButtonsContainer";

    /// <summary>The name of the overlay panel template part.</summary>
    public const string OverlayPanelPartName = "OverlayPanel";

    /// <summary>The name of the pin button template part.</summary>
    public const string PinButtonPartName = "PartPinButton";

    /// <summary>The name of the close button template part.</summary>
    public const string CloseButtonPartName = "PartCloseButton";

    /// <summary>The name of the pinned indicator template part.</summary>
    public const string PinnedIndicatorPartName = "PartPinnedIndicator";

    /// <summary>The name of the common visual states group.</summary>
    public const string CommonVisualStates = "CommonStates";

    /// <summary>The name of the pin visual states group.</summary>
    public const string PinVisualStates = "PinStates";

    /// <summary>The name of the overlay visual states group.</summary>
    public const string OverlayVisualStates = "OverlayStates";

    /// <summary>The name of the normal visual state.</summary>
    public const string NormalVisualState = "Normal";

    /// <summary>The name of the pointer over visual state.</summary>
    public const string PointerOverVisualState = "PointerOver";

    /// <summary>The name of the selected visual state.</summary>
    public const string SelectedVisualState = "Selected";

    /// <summary>The name of the selected pointer over visual state.</summary>
    public const string SelectedPointerOverVisualState = "SelectedPointerOver";

    /// <summary>The name of the unpinned visual state.</summary>
    public const string UnpinnedVisualState = "Unpinned";

    /// <summary>The name of the pinned visual state.</summary>
    public const string PinnedVisualState = "Pinned";

    /// <summary>The name of the overlay hidden visual state.</summary>
    public const string OverlayHiddenVisualState = "OverlayHidden";

    /// <summary>The name of the overlay visible visual state.</summary>
    public const string OverlayVisibleVisualState = "OverlayVisible";

    /// <summary>The name of the dragging visual states group.</summary>
    public const string DraggingVisualStates = "DraggingStates";

    /// <summary>The name of the not dragging visual state.</summary>
    public const string NotDraggingVisualState = "NotDragging";

    /// <summary>The name of the dragging visual state.</summary>
    public const string DraggingVisualState = "Dragging";

    /// <summary>
    ///     Minimum space that must remain for dragging the tab, even when the tool buttons are overlayed.
    /// </summary>
    internal const int MinDragWidth = 60;

    // Required parts will not be nulll after OnApplyTemplate is called
    private StackPanel buttonsContainer = null!;
    private Button pinButton = null!;
    private Button closeButton = null!;

    // Optional parts we manage in code behind
    private UIElement? pinnedIndicator;
    private IconSourceElement? iconPart;
    private TextBlock? headerPart;
    private ILogger? logger;
    private ILogger? visualLogger;

    // Keeps track of whether the pointer is currently over the control
    private bool isPointerOver;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStripItem"/> class.
    /// </summary>
    public TabStripItem()
    {
        this.DefaultStyleKey = typeof(TabStripItem);

        // Ensure we clean up subscriptions when the control is unloaded to avoid
        // leaking references from recycled containers (ItemsRepeater) or when
        // templates are removed.
        this.Unloaded += this.TabStripItem_Unloaded;
    }

    /// <summary>
    ///     Applies the control template and sets up required and optional template parts.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown if a required template part is missing or of the wrong type.</exception>
    [MemberNotNull(nameof(buttonsContainer), nameof(pinButton), nameof(closeButton))]
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.LogApplyingTemplate();

        T? GetTemplatePart<T>(string name, bool isRequired = false)
            where T : DependencyObject
        {
            var part = this.GetTemplateChild(name) as T;
            if (part is null)
            {
                var expectedType = typeof(T);
                this.LogTemplatePartNotFound(name, expectedType, isRequired);
                if (isRequired)
                {
                    throw new InvalidOperationException($"The required template part '{name}' is missing or is not of type '{expectedType}'.");
                }
            }

            return part;
        }

        T GetRequiredTemplatePart<T>(string name)
            where T : DependencyObject
            => GetTemplatePart<T>(name, isRequired: true)!;

        // Required parts
        this.buttonsContainer = GetRequiredTemplatePart<StackPanel>(ButtonsContainerPartName);
        this.pinButton = GetRequiredTemplatePart<Button>(PinButtonPartName);
        this.closeButton = GetRequiredTemplatePart<Button>(CloseButtonPartName);
        _ = GetRequiredTemplatePart<Grid>(RootGridPartName); // required to exist for VSM

        // Optional parts, that we managed in code behind
        this.iconPart = GetTemplatePart<IconSourceElement>(IconPartName);
        this.headerPart = GetTemplatePart<TextBlock>(HeaderPartName);
        this.pinnedIndicator = GetTemplatePart<UIElement>(PinnedIndicatorPartName);

        // Work around template/data context initialization order: if we already have an Item,
        // manually update all critical visual elements that depend on bindings, since ItemsRepeater
        // recycling or template application order may cause bindings to be lost or not updated.
        if (this.Item is not null)
        {
            this.ApplyItemProperties();
        }

        // Attach button click events for the tool buttons
        this.pinButton.Click -= PinButton_Click;
        this.pinButton.Click += PinButton_Click;
        this.closeButton.Click -= CloseButton_Click;
        this.closeButton.Click += CloseButton_Click;

        this.UpdateMinWidth();
        this.UpdateVisualStates(useTransitions: false);

        void PinButton_Click(object sender, RoutedEventArgs e) => this.OnPinClicked();
        void CloseButton_Click(object sender, RoutedEventArgs e) => this.OnCloseClicked();
    }

    /// <summary>
    ///     Handles pointer entered events and updates visual state.
    /// </summary>
    /// <param name="e">Pointer event data.</param>
    protected override void OnPointerEntered(PointerRoutedEventArgs e)
    {
        base.OnPointerEntered(e);
        this.LogPointerEntered(e);
        this.OnPointerEntered();
    }

    /// <summary>
    ///     Called when the pointer enters the control. Updates state and toolbar visibility.
    /// </summary>
    protected virtual void OnPointerEntered()
    {
        if (!this.IsEnabled)
        {
            return;
        }

        this.isPointerOver = true;
        this.UpdateVisualStates();
    }

    /// <summary>
    ///     Handles pointer exited events and updates visual state.
    /// </summary>
    /// <param name="e">Pointer event data.</param>
    protected override void OnPointerExited(PointerRoutedEventArgs e)
    {
        base.OnPointerExited(e);
        this.LogPointerExited();
        this.OnPointerExited();
    }

    /// <summary>
    ///     Called when the pointer exits the control. Updates state and toolbar visibility.
    /// </summary>
    protected virtual void OnPointerExited()
    {
        this.isPointerOver = false;

        if (!this.IsEnabled)
        {
            return;
        }

        this.UpdateVisualStates();
    }

    /// <summary>
    ///     Called when the pin button is clicked. Toggles the pinned state.
    /// </summary>
    protected virtual void OnPinClicked()
    {
        if (!this.IsEnabled)
        {
            return;
        }

        Debug.Assert(this.Item is not null, "a control with a null Item must be disabled");

        this.Item.IsPinned = !this.Item.IsPinned;
        this.LogPinClicked();
    }

    /// <summary>
    ///     Called when the close button is clicked. Raises the close requested event.
    /// </summary>
    protected virtual void OnCloseClicked()
    {
        if (!this.IsEnabled)
        {
            return;
        }

        Debug.Assert(this.Item is not null, "a control with a null Item must be disabled");

        this.CloseRequested?.Invoke(this, new TabCloseRequestedEventArgs { Item = this.Item });
        this.LogCloseRequested();
    }

    private void TabStripItem_Unloaded(object? sender, RoutedEventArgs e)
    {
        // If we still have an item subscribed, unsubscribe to avoid leaking
        // event handlers when the container is recycled.
        if (this.Item is TabItem item)
        {
            item.PropertyChanged -= this.TabItem_OnPropertyChanged;
        }

        // Clear logger reference; the factory is owned by the app and should
        // not be retained by long-lived references on recycled visuals.
        this.logger = null;
    }

    private void ApplyItemProperties()
    {
        Debug.Assert(this.Item is not null, "ApplyItemProperties should only be called when Item is not null.");

        if (this.iconPart is not null)
        {
            this.iconPart.IconSource = this.Item.Icon;
            this.iconPart.Visibility = this.Item.Icon is not null ? Visibility.Visible : Visibility.Collapsed;
        }

        _ = this.headerPart?.Text = this.Item.Header;

        _ = this.pinnedIndicator?.Visibility = this.Item.IsPinned ? Visibility.Visible : Visibility.Collapsed;
    }

    /// <summary>
    ///     Handles changes to the Item property and updates bindings and state.
    /// </summary>
    /// <param name="e">Dependency property change event data.</param>
    private void OnItemChanged(DependencyPropertyChangedEventArgs e)
    {
        this.LogItemChanged(e);

        // Do NOT set DataContext here: the control template sets DataContext via
        // DataContext="{TemplateBinding Item}" on the root grid. Setting the
        // control-level DataContext interferes with template bindings when
        // containers are recycled by ItemsRepeater. Only set Content for any
        // ContentPresenters that may rely on it.
        this.Content = e.NewValue; // For ContentPresenter if needed

        if (e.OldValue is TabItem oldItem)
        {
            oldItem.PropertyChanged -= this.TabItem_OnPropertyChanged;
        }

        if (e.NewValue is not TabItem newItem)
        {
            // With no Item, the control is useless
            this.IsEnabled = false;
            this.LogEnabledOrDisabled();
            return;
        }

        this.IsEnabled = true;
        this.ApplyItemProperties();
        this.LogEnabledOrDisabled();
        newItem.PropertyChanged += this.TabItem_OnPropertyChanged;

        this.UpdateVisualStates();
    }

    /// <summary>
    ///     Handles property changes on the bound TabItem and updates visual state as needed.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">Property changed event data.</param>
    private void TabItem_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(TabItem.IsSelected), System.StringComparison.Ordinal))
        {
            this.LogItemPropertyChanged(e);
            this.UpdateVisualStates();
        }
        else if (string.Equals(e.PropertyName, nameof(TabItem.IsPinned), System.StringComparison.Ordinal))
        {
            this.LogItemPropertyChanged(e);
            this.UpdateVisualStates();
        }
    }

    /// <summary>
    ///     Updates the visual states of the control based on selection and pointer state.
    /// </summary>
    /// <param name="useTransitions">Whether to use visual transitions.</param>
    private void UpdateVisualStates(bool useTransitions = true)
    {
        if (this.Item is null)
        {
            // An item being recycled may temporarily have no Item
            return;
        }

        var basicState = this.Item.IsSelected
            ? this.isPointerOver
                ? SelectedPointerOverVisualState
                : SelectedVisualState
            : this.isPointerOver
                ? PointerOverVisualState
                : NormalVisualState;

        this.LogVisualState(basicState);
        _ = VisualStateManager.GoToState(this, basicState, useTransitions);

        var pinState = this.Item?.IsPinned == true ? PinnedVisualState : UnpinnedVisualState;
        this.LogVisualState(pinState);
        _ = VisualStateManager.GoToState(this, pinState, useTransitions: false);

        var draggingState = this.IsDragging ? DraggingVisualState : NotDraggingVisualState;
        this.LogVisualState(draggingState);
        _ = VisualStateManager.GoToState(this, draggingState, useTransitions: false);

        var overlayState = this.IsDragging
            ? OverlayHiddenVisualState
            : this.isPointerOver
                ? OverlayVisibleVisualState
                : OverlayHiddenVisualState;
        this.LogVisualState(overlayState);
        _ = VisualStateManager.GoToState(this, overlayState, useTransitions);
    }

    /// <summary>
    ///     Updates the minimum width of the tab item based on button and margin sizes.
    /// </summary>
    private void UpdateMinWidth()
    {
        var marginLeft = this.buttonsContainer?.Margin.Left ?? 0;
        var marginRight = this.buttonsContainer?.Margin.Right ?? 0;
        var pinWidth = this.pinButton?.Width ?? 0;
        var closeWidth = this.closeButton?.Width ?? 0;
        var spacing = (pinWidth > 0 && closeWidth > 0) ? this.buttonsContainer?.Spacing ?? 0 : 0;
        this.MinWidth = MinDragWidth + marginLeft + pinWidth + closeWidth + spacing + marginRight;

        this.LogMinWidthUpdated();
    }

    /// <summary>
    ///     Handles changes to the IsCompact property and updates toolbar visibility.
    /// </summary>
    /// <param name="oldValue">Previous value of IsCompact.</param>
    /// <param name="newValue">New value of IsCompact.</param>
    /// <remarks>
    ///     In the current implementation, IsCompact has no effect on the layout of the control.
    /// </remarks>
    private void OnIsCompactChanged(bool oldValue, bool newValue) =>
        this.LogCompactModeChanged(oldValue, newValue);
}
