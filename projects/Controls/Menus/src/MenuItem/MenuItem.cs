// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Represents an individual menu item control that renders a four-column layout with Icon, Text, Accelerator, and State.
///     This is the foundation control used by all menu containers (MenuBar, MenuFlyout, ExpandableMenuBar).
/// </summary>
/// <remarks>
///     <para>
///         The <see cref="MenuItem" /> control is designed to provide a consistent presentation layer for menu items
///         across all menu systems. It renders a standardized four-column layout: Icon|Text|Accelerator|State,
///         ensuring visual consistency whether the item appears in a menu bar, flyout, or expandable menu.
///     </para>
///     <para>
///         The control supports all menu item types including commands, separators, checkable items, and grouped
///         (radio-style) selections. Visual states provide appropriate feedback for user interactions, hover states,
///         selection states, and disabled conditions.
///     </para>
///     <para>
///         <strong>Column Layout</strong>
///     </para>
///     <para>
///         - **Column 1 (Icon)**: 16×16px semantic icon or left-side checkmark (when no icon present),
///         - **Column 2 (Text)**: Menu item text, left-aligned and expandable,
///         - **Column 3 (Accelerator)**: Keyboard shortcut text, right-aligned,
///         - **Column 4 (State)**: Selection indicators (✓) or submenu arrows (►), right-aligned.
///     </para>
///     <para>
///         <strong>Usage Guidelines</strong>
///     </para>
///     <para>
///         This control is primarily intended for internal use by menu container controls. It binds to a
///         <see cref="MenuItemData" /> instance that provides all necessary display and behavior information.
///         The control handles visual state management, icon rendering, and user interaction feedback.
///     </para>
/// </remarks>
/// <example>
///     <para>
///         <strong>Example Usage in Container Control</strong>
///     </para>
///     <![CDATA[
/// <!-- Used internally by menu containers -->
/// <ItemsRepeater ItemsSource="{x:Bind MenuItems}">
///     <ItemsRepeater.ItemTemplate>
///         <DataTemplate>
///             <controls:MenuItem ItemData="{Binding}" />
///         </DataTemplate>
///     </ItemsRepeater.ItemTemplate>
/// </ItemsRepeater>
/// ]]>
/// </example>
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = PointerOverVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = PressedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = DisabledVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = CheckedNoIconVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = CheckedWithIconVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = WithChildrenVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = NoDecorationVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = ActiveVisualState, GroupName = NavigationVisualStates)]
[TemplateVisualState(Name = InactiveVisualState, GroupName = NavigationVisualStates)]
[TemplateVisualState(Name = HasIconVisualState, GroupName = IconVisualStates)]
[TemplateVisualState(Name = NoIconVisualState, GroupName = IconVisualStates)]
[TemplateVisualState(Name = HasAcceleratorVisualState, GroupName = AcceleratorVisualStates)]
[TemplateVisualState(Name = NoAcceleratorVisualState, GroupName = AcceleratorVisualStates)]
[TemplateVisualState(Name = SeparatorVisualState, GroupName = TypeVisualStates)]
[TemplateVisualState(Name = ItemVisualState, GroupName = TypeVisualStates)]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[TemplatePart(Name = ContentGridPart, Type = typeof(Grid))]
[TemplatePart(Name = IconPresenterPart, Type = typeof(IconSourceElement))]
[TemplatePart(Name = TextBlockPart, Type = typeof(TextBlock))]
[TemplatePart(Name = AcceleratorTextBlockPart, Type = typeof(TextBlock))]
[TemplatePart(Name = StateTextBlockPart, Type = typeof(TextBlock))]
[TemplatePart(Name = SeparatorBorderPart, Type = typeof(Border))]
[TemplatePart(Name = SubmenuArrowPart, Type = typeof(TextBlock))]
[TemplatePart(Name = CheckmarkPart, Type = typeof(TextBlock))]
public partial class MenuItem : ContentControl
{
    // Template Part Names

    /// <summary>
    ///     The name of the root Grid element in the control template (required).
    /// </summary>
    public const string RootGridPart = "PartRootGrid";

    /// <summary>
    ///     The name of the content Grid that hosts the item layout (icon/text/accelerator/state).
    /// </summary>
    public const string ContentGridPart = "PartContentGrid";

    /// <summary>
    ///     The name of the Icon presenter element (binds <c>ItemData.Icon</c>).
    /// </summary>
    public const string IconPresenterPart = "PartIconPresenter";

    /// <summary>
    ///     The name of the TextBlock that displays the menu item's text.
    /// </summary>
    public const string TextBlockPart = "PartTextBlock";

    /// <summary>
    ///     The name of the TextBlock that displays accelerator/shortcut text.
    /// </summary>
    public const string AcceleratorTextBlockPart = "PartAcceleratorTextBlock";

    /// <summary>
    ///     The name of the TextBlock used for the right-side state checkmark glyph.
    /// </summary>
    public const string StateTextBlockPart = "PartStateTextBlock";

    /// <summary>
    ///     The name of the Border used to render a separator line when <c>ItemData.IsSeparator</c> is true.
    /// </summary>
    public const string SeparatorBorderPart = "PartSeparatorBorder";

    /// <summary>
    ///     The name of the submenu arrow element (shown when item has children).
    /// </summary>
    public const string SubmenuArrowPart = "PartSubmenuArrow";

    /// <summary>
    ///     The name of the left-side checkmark element (shown when no icon is present and item is checked).
    /// </summary>
    public const string CheckmarkPart = "PartCheckmark";

    // Visual State Names

    /// <summary>
    ///     Group name for common interaction visual states (Normal/PointerOver/Pressed/Disabled).
    /// </summary>
    public const string CommonVisualStates = "CommonStates";

    /// <summary>
    ///     Visual state for the normal (default) appearance.
    /// </summary>
    public const string NormalVisualState = "Normal";

    /// <summary>
    ///     Visual state when the pointer is over the item.
    /// </summary>
    public const string PointerOverVisualState = "PointerOver";

    /// <summary>
    ///     Visual state when the item is being pressed.
    /// </summary>
    public const string PressedVisualState = "Pressed";

    /// <summary>
    ///     Visual state when the item is disabled.
    /// </summary>
    public const string DisabledVisualState = "Disabled";

    /// <summary>
    ///     Group name for decoration/checkmark visual states.
    /// </summary>
    public const string DecorationVisualStates = "DecorationStates";

    /// <summary>
    ///     Visual state when an item is checked and no icon occupies the left column.
    /// </summary>
    public const string CheckedNoIconVisualState = "CheckedNoIcon";

    /// <summary>
    ///     Visual state when an item is checked and an icon is present (checkmark shown on right side).
    /// </summary>
    public const string CheckedWithIconVisualState = "CheckedWithIcon";

    /// <summary>
    ///     Visual state used when the item has children (submenu arrow should be shown).
    /// </summary>
    public const string WithChildrenVisualState = "WithChildren";

    /// <summary>
    ///     Visual state when no decoration (checkmark/submenu arrow) should be shown.
    /// </summary>
    public const string NoDecorationVisualState = "NoDecoration";

    /// <summary>
    ///     Group name for navigation (active/inactive) visual states.
    /// </summary>
    public const string NavigationVisualStates = "NavigationStates";

    /// <summary>
    ///     Visual state representing keyboard-activated (hot-tracked) item.
    /// </summary>
    public const string ActiveVisualState = "Active";

    /// <summary>
    ///     Visual state representing non-active keyboard navigation state.
    /// </summary>
    public const string InactiveVisualState = "Inactive";

    /// <summary>
    ///     Group name for icon presence visual states.
    /// </summary>
    public const string IconVisualStates = "IconStates";

    /// <summary>
    ///     Visual state when the item has an icon.
    /// </summary>
    public const string HasIconVisualState = "HasIcon";

    /// <summary>
    ///     Visual state when the item has no icon.
    /// </summary>
    public const string NoIconVisualState = "NoIcon";

    /// <summary>
    ///     Group name for accelerator presence visual states.
    /// </summary>
    public const string AcceleratorVisualStates = "AcceleratorStates";

    /// <summary>
    ///     Visual state when the item has accelerator/shortcut text.
    /// </summary>
    public const string HasAcceleratorVisualState = "HasAccelerator";

    /// <summary>
    ///     Visual state when the item has no accelerator text.
    /// </summary>
    public const string NoAcceleratorVisualState = "NoAccelerator";

    /// <summary>
    ///     Group name for type states (Separator vs Item).
    /// </summary>
    public const string TypeVisualStates = "TypeStates";

    /// <summary>
    ///     Visual state used when the control is rendered as a separator.
    /// </summary>
    public const string SeparatorVisualState = "Separator";

    /// <summary>
    ///     Visual state used when the control is rendered as a normal item.
    /// </summary>
    public const string ItemVisualState = "Item";

    // Template Parts (will be set in OnApplyTemplate)
    private Grid? rootGrid;
    private Grid? contentGrid;
    private IconSourceElement? iconPresenter;
    private TextBlock? textBlock;
    private TextBlock? acceleratorTextBlock;
    private TextBlock? stateTextBlock;
    private Border? separatorBorder;
    private TextBlock? submenuArrow;
    private TextBlock? checkmark;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuItem" /> class.
    /// </summary>
    public MenuItem()
    {
        this.DefaultStyleKey = typeof(MenuItem);

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private bool IsPressed =>
        this.PointerCaptures?.Any(p => p.PointerDeviceType == PointerDeviceType.Mouse) == true;

    private bool IsPointerOver { get; set; }

    /// <summary>
    ///     Called when the template is applied to the control.
    ///     Sets up template parts and initializes visual states.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        // Validate required parts exist
        this.rootGrid = this.GetTemplateChild(RootGridPart) as Grid ??
            throw new InvalidOperationException($"{nameof(MenuItem)} template is missing {RootGridPart}");

        // Get template parts
        this.SetupTemplateParts();

        // Initialize visual states based on current ItemData
        this.UpdateAllVisualStates();

        base.OnApplyTemplate();
    }

    /// <summary>
    ///     Called when the control receives keyboard focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnGotFocus(RoutedEventArgs e)
    {
        this.UpdateInteractionVisualState();
        base.OnGotFocus(e);
    }

    /// <summary>
    ///     Called when the control loses keyboard focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnLostFocus(RoutedEventArgs e)
    {
        this.UpdateInteractionVisualState();
        base.OnLostFocus(e);
    }

    /// <summary>
    ///     Called when a key is pressed while the control has focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnKeyDown(KeyRoutedEventArgs e)
    {
        if (this.ItemData?.IsSeparator == true)
        {
            base.OnKeyDown(e);
            return;
        }

        switch (e.Key)
        {
            case VirtualKey.Enter:
            case VirtualKey.Space:
                if (this.ItemData?.IsEnabled == true)
                {
                    this.ExecuteCommand();
                    e.Handled = true;
                }

                break;

            case VirtualKey.Right:
                if (this.ItemData?.HasChildren == true)
                {
                    this.ExpandSubmenu();
                    e.Handled = true;
                }

                break;
        }

        base.OnKeyDown(e);
    }

    /// <summary>
    ///     Handles pointer enter events from the visual tree and updates hover state.
    ///     Raises <c>HoverEntered</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        if (this.ItemData?.IsSeparator == true)
        {
            this.IsPointerOver = false;
            this.UpdateInteractionVisualState();
            this.UpdateActiveVisualState();
            return;
        }

        this.IsPointerOver = true;

        if (this.ItemData?.IsSeparator != true && this.ItemData?.IsEnabled == true)
        {
            this.HoverEntered?.Invoke(this, new MenuItemHoverEventArgs { MenuItem = this.ItemData! });
        }

        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
    }

    /// <summary>
    ///     Handles pointer exit events from the visual tree and clears hover state.
    ///     Raises <c>HoverExited</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerExited(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        this.IsPointerOver = false;

        if (this.ItemData?.IsSeparator != true)
        {
            this.HoverExited?.Invoke(this, new MenuItemHoverEventArgs { MenuItem = this.ItemData! });
        }

        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
    }

    /// <summary>
    ///     Handles pointer pressed events and updates the pressed visual state when appropriate.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        if (this.ItemData?.IsEnabled == true && this.ItemData.IsSeparator != true)
        {
            this.UpdateInteractionVisualState();
            this.UpdateActiveVisualState();
        }
    }

    /// <summary>
    ///     Handles pointer release events and re-evaluates the interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
    }

    /// <summary>
    ///     Handles tapped events (activation). If the item has children, requests submenu
    ///     expansion; otherwise handles selection, executes any command, and raises
    ///     the <c>Invoked</c> event.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Tap event arguments (may be marked handled).</param>
    protected void OnTapped(object sender, TappedRoutedEventArgs e)
    {
        if (this.ItemData?.IsEnabled == true && !this.ItemData.IsSeparator)
        {
            if (this.ItemData.HasChildren)
            {
                this.ExpandSubmenu();
            }
            else
            {
                // Handle selection state first (always, independent of commands)
                this.HandleSelectionState();

                // Then execute command if present
                this.ExecuteCommand();

                // Always raise invoked event (even without command)
                this.Invoked?.Invoke(this, new MenuItemInvokedEventArgs { MenuItem = this.ItemData });
            }

            e.Handled = true;
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.PointerPressed += this.OnPointerPressed;
        this.PointerReleased += this.OnPointerReleased;
        this.Tapped += this.OnTapped;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        // Clean up event handlers to prevent memory leaks
        this.Loaded -= this.OnLoaded;
        this.Unloaded -= this.OnUnloaded;
        this.PointerEntered -= this.OnPointerEntered;
        this.PointerExited -= this.OnPointerExited;
        this.PointerPressed -= this.OnPointerPressed;
        this.PointerReleased -= this.OnPointerReleased;
        this.Tapped -= this.OnTapped;
    }

    private void SetupTemplateParts()
    {
        this.contentGrid = this.GetTemplateChild(ContentGridPart) as Grid;
        this.iconPresenter = this.GetTemplateChild(IconPresenterPart) as IconSourceElement;
        this.textBlock = this.GetTemplateChild(TextBlockPart) as TextBlock;
        this.acceleratorTextBlock = this.GetTemplateChild(AcceleratorTextBlockPart) as TextBlock;
        this.stateTextBlock = this.GetTemplateChild(StateTextBlockPart) as TextBlock;
        this.separatorBorder = this.GetTemplateChild(SeparatorBorderPart) as Border;
        this.submenuArrow = this.GetTemplateChild(SubmenuArrowPart) as TextBlock;
        this.checkmark = this.GetTemplateChild(CheckmarkPart) as TextBlock;
    }

    private void UpdateAllVisualStates()
    {
        this.UpdateTypeVisualState();
        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
        this.UpdateIconVisualState();
        this.UpdateAcceleratorVisualState();
        this.UpdateCheckmarkVisualState();
    }

    private void UpdateTypeVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemData?.IsSeparator == true ? SeparatorVisualState : ItemVisualState,
            useTransitions: true);

    private void UpdateInteractionVisualState()
    {
        if (this.ItemData?.IsSeparator == true)
        {
            this.IsPointerOver = false;
            _ = VisualStateManager.GoToState(this, NormalVisualState, useTransitions: true);
            return;
        }

        if (this.ItemData?.IsEnabled != true)
        {
            _ = VisualStateManager.GoToState(this, DisabledVisualState, useTransitions: true);
        }
        else if (this.ItemData?.IsActive == true)
        {
            _ = VisualStateManager.GoToState(this, PointerOverVisualState, useTransitions: true);
        }
        else if (this.IsPressed)
        {
            _ = VisualStateManager.GoToState(this, PressedVisualState, useTransitions: true);
        }
        else if (this.IsPointerOver)
        {
            _ = VisualStateManager.GoToState(this, PointerOverVisualState, useTransitions: true);
        }
        else
        {
            _ = VisualStateManager.GoToState(this, NormalVisualState, useTransitions: true);
        }
    }

    private void UpdateActiveVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemData?.IsActive == true ? ActiveVisualState : InactiveVisualState,
            useTransitions: true);

    private void UpdateIconVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemData?.Icon is not null ? HasIconVisualState : NoIconVisualState,
            useTransitions: true);

    private void UpdateAcceleratorVisualState()
        => VisualStateManager.GoToState(
            this,
            !string.IsNullOrEmpty(this.ItemData?.AcceleratorText) ? HasAcceleratorVisualState : NoAcceleratorVisualState,
            useTransitions: true);

    private void UpdateCheckmarkVisualState()
    {
        if (this.ItemData == null)
        {
            _ = VisualStateManager.GoToState(this, NoDecorationVisualState, useTransitions: true);
            return;
        }

        // Priority order: Submenu Arrow > Selection State > Nothing
        if (this.ItemData.HasChildren && this.ShowSubmenuGlyph)
        {
            _ = VisualStateManager.GoToState(this, WithChildrenVisualState, useTransitions: true);
        }
        else if (this.ItemData.HasSelectionState && this.ItemData.IsChecked)
        {
            // Show checkmark on right side if item has icon, left side if no icon
            var stateName = this.ItemData.Icon != null ? CheckedWithIconVisualState : CheckedNoIconVisualState;
            _ = VisualStateManager.GoToState(this, stateName, useTransitions: true);
        }
        else
        {
            _ = VisualStateManager.GoToState(this, NoDecorationVisualState, useTransitions: true);
        }
    }

    private void ExecuteCommand()
    {
        // Only execute command if present and can execute
        if (this.ItemData?.Command?.CanExecute(this.ItemData) == true)
        {
            this.ItemData.Command.Execute(this.ItemData);
        }
    }

    private void HandleSelectionState()
    {
        if (this.ItemData == null)
        {
            return;
        }

        if (!string.IsNullOrEmpty(this.ItemData.RadioGroupId))
        {
            // Handle radio group behavior - raise event to let container handle group logic
            this.RadioGroupSelectionRequested?.Invoke(
                this,
                new MenuItemRadioGroupEventArgs
                {
                    MenuItem = this.ItemData,
                    GroupId = this.ItemData.RadioGroupId,
                });
        }
        else if (this.ItemData.IsCheckable)
        {
            // Handle individual checkable item - just toggle
            this.ItemData.IsChecked = !this.ItemData.IsChecked;
        }
    }

    private void ExpandSubmenu()
    {
        if (this.ItemData?.HasChildren == true)
        {
            this.SubmenuRequested?.Invoke(this, new MenuItemSubmenuEventArgs { MenuItem = this.ItemData });
        }
    }
}
