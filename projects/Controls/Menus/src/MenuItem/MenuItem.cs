// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or <see cref="MenuFlyout"/>.
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
public partial class MenuItem : Control
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
    private bool isMnemonicDisplayVisible;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuItem" /> class.
    /// </summary>
    public MenuItem()
    {
        this.DefaultStyleKey = typeof(MenuItem);

        /*
         * We only subscribe to events within the control itself; no need to
         * over-engineer with Loaded/Unloaded and no need to unsubscribe
         * explicitly as it will be done when the control is disposed.
         */

        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.PointerPressed += this.OnPointerPressed;
        this.PointerReleased += this.OnPointerReleased;
        this.Tapped += this.OnTapped;

        this.AccessKeyDisplayRequested += this.OnAccessKeyDisplayRequested;
        this.AccessKeyDisplayDismissed += this.OnAccessKeyDisplayDismissed;

        this.Unloaded += (_, _) => this.ItemData?.PropertyChanged -= this.ItemData_OnPropertyChanged;
    }

    private bool IsPressed =>
        this.PointerCaptures?.Any(p => p.PointerDeviceType == PointerDeviceType.Mouse) == true;

    private bool IsPointerOver { get; set; }

    /// <summary>
    ///     Attempts to expand the submenu if the item has children, or invokes the item's command or selection
    ///     logic if not. Updates visual states accordingly.
    /// </summary>
    /// <returns>
    ///     True if the submenu was expanded or the item was invoked; otherwise, false.
    /// </returns>
    internal bool TryExpandOrInvoke()
    {
        if (!this.IsInteractiveItem())
        {
            return false;
        }

        var handled = this.TryExpandSubmenu() || this.TryInvoke();

        if (handled)
        {
            this.UpdateInteractionVisualState();
            this.UpdateActiveVisualState();
        }

        return handled;
    }

    /// <summary>
    /// Creates an automation peer for this control so automation tools and access keys
    /// can interact with the MenuItem (for example invoking it via UI Automation).
    /// </summary>
    /// <returns>
    /// A <see cref="MenuItemAutomationPeer"/> instance for this control.
    /// </returns>
    protected override AutomationPeer OnCreateAutomationPeer() => new MenuItemAutomationPeer(this);

    /// <summary>
    ///     Called when the template is applied to the control.
    ///     Sets up template parts and initializes visual states.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        // Validate required parts exist
        this.rootGrid = this.GetTemplateChild(RootGridPart) as Grid ??
            throw new InvalidOperationException($"{nameof(MenuItem)} template is missing {RootGridPart}");

        // Setup other template parts (optional)
        this.contentGrid = this.GetTemplateChild(ContentGridPart) as Grid;
        this.iconPresenter = this.GetTemplateChild(IconPresenterPart) as IconSourceElement;
        this.textBlock = this.GetTemplateChild(TextBlockPart) as TextBlock;
        this.acceleratorTextBlock = this.GetTemplateChild(AcceleratorTextBlockPart) as TextBlock;
        this.stateTextBlock = this.GetTemplateChild(StateTextBlockPart) as TextBlock;
        this.separatorBorder = this.GetTemplateChild(SeparatorBorderPart) as Border;
        this.submenuArrow = this.GetTemplateChild(SubmenuArrowPart) as TextBlock;
        this.checkmark = this.GetTemplateChild(CheckmarkPart) as TextBlock;

        // Initialize visual states based on current ItemData
        this.UpdateTypeVisualState();
        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
        this.UpdateIconVisualState();
        this.UpdateAcceleratorVisualState();
        this.UpdateCheckmarkVisualState();

        this.RefreshTextPresentation();

        // In the scenario where the template is reapplied, ensure we rewire PropertyChanged from ItemData
        this.ItemData?.PropertyChanged -= this.ItemData_OnPropertyChanged;
        this.ItemData?.PropertyChanged += this.ItemData_OnPropertyChanged;
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
        if (!this.IsInteractiveItem())
        {
            base.OnKeyDown(e);
            return;
        }

        var data = this.ItemData!; // already validated by IsInteractiveItem
        var handled = e.Key switch
        {
            VirtualKey.Enter or VirtualKey.Space => this.TryExpandOrInvoke(),
            VirtualKey.Right when data.HasChildren => this.TryExpandSubmenu(),
            _ => false,
        };

        if (handled)
        {
            e.Handled = true;
            this.UpdateInteractionVisualState();
            this.UpdateActiveVisualState();
        }
        else
        {
            base.OnKeyDown(e);
        }
    }

    /// <summary>
    ///     Handles pointer enter events from the visual tree and updates hover state.
    ///     Raises <c>HoverStarted</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        if (!this.IsInteractiveItem())
        {
            this.IsPointerOver = false;
            return;
        }

        this.IsPointerOver = true;

        this.HoverStarted?.Invoke(this, new MenuItemHoverEventArgs { ItemData = this.ItemData! });

        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
    }

    /// <summary>
    ///     Handles pointer exit events from the visual tree and clears hover state.
    ///     Raises <c>HoverEnded</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    protected void OnPointerExited(object sender, PointerRoutedEventArgs e)
    {
        _ = sender; // unused
        _ = e; // unused

        this.IsPointerOver = false;

        if (!this.IsInteractiveItem())
        {
            return;
        }

        this.HoverEnded?.Invoke(this, new MenuItemHoverEventArgs { ItemData = this.ItemData! });

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

        if (!this.IsInteractiveItem())
        {
            return;
        }

        this.UpdateInteractionVisualState();
        this.UpdateActiveVisualState();
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

        if (!this.IsInteractiveItem())
        {
            return;
        }

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
    protected void OnTapped(object sender, TappedRoutedEventArgs e) => e.Handled = this.TryExpandOrInvoke();

    private bool IsInteractiveItem() =>
        this.ItemData is { IsEnabled: true, IsSeparator: false };

    private void UpdateTypeVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemData?.IsSeparator == true ? SeparatorVisualState : ItemVisualState,
            useTransitions: true);

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

    private void UpdateInteractionVisualState()
    {
        if (this.ItemData is not { } data)
        {
            _ = VisualStateManager.GoToState(this, NormalVisualState, useTransitions: true);
            return;
        }

        if (data.IsSeparator)
        {
            this.IsPointerOver = false;
            _ = VisualStateManager.GoToState(this, NormalVisualState, useTransitions: true);
            return;
        }

        var state = !data.IsEnabled ? DisabledVisualState
                  : this.IsPressed ? PressedVisualState
                  : data.IsActive || this.IsPointerOver ? PointerOverVisualState
                  : NormalVisualState;

        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    private void UpdateCheckmarkVisualState()
    {
        // Priority order: Submenu Arrow > Selection State > Nothing
        // Show checkmark on right side if item has icon, left side if no icon
        var state = this.ItemData is not { } data
            ? NoDecorationVisualState
            : (data.HasChildren && this.ShowSubmenuGlyph)
                ? WithChildrenVisualState
                : (data.HasSelectionState && data.IsChecked)
                    ? (data.Icon != null ? CheckedWithIconVisualState : CheckedNoIconVisualState)
                    : NoDecorationVisualState;

        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    private void HandleSelectionState()
    {
        Debug.Assert(this.ItemData is { }, "ItemData should be non-null");

        var data = this.ItemData!; // already validated by assert above
        if (!string.IsNullOrEmpty(data.RadioGroupId))
        {
            // Handle radio group behavior - raise event to let container handle group logic
            this.RadioGroupSelectionRequested?.Invoke(
                this,
                new MenuItemRadioGroupEventArgs
                {
                    ItemData = data,
                    GroupId = data.RadioGroupId,
                });
        }
        else if (data.IsCheckable)
        {
            // Handle individual checkable item - just toggle
            data.IsChecked = !data.IsChecked;
        }
    }

    /// <summary>
    ///     Updates the visibility of the mnemonic underline programmatically.
    /// </summary>
    /// <param name="isVisible">True to show the mnemonic underline; false to hide it.</param>
    private void SetMnemonicVisibility(bool isVisible)
    {
        if (this.isMnemonicDisplayVisible == isVisible)
        {
            return;
        }

        this.isMnemonicDisplayVisible = isVisible;
        this.RefreshTextPresentation();
    }

    private void OnAccessKeyDisplayRequested(UIElement sender, AccessKeyDisplayRequestedEventArgs args)
        => this.SetMnemonicVisibility(true);

    private void OnAccessKeyDisplayDismissed(UIElement sender, AccessKeyDisplayDismissedEventArgs args)
        => this.SetMnemonicVisibility(false);

    private void RefreshTextPresentation()
    {
        if (this.textBlock is null)
        {
            return;
        }

        if (this.DispatcherQueue is { } dispatcher && !dispatcher.HasThreadAccess)
        {
            _ = dispatcher.TryEnqueue(() => this.UpdateTextBlockContent(this.isMnemonicDisplayVisible));
            return;
        }

        this.UpdateTextBlockContent(this.isMnemonicDisplayVisible);
    }

    private void UpdateTextBlockContent(bool showMnemonicUnderline)
    {
        if (this.textBlock is null)
        {
            return;
        }

        var text = this.ItemData?.Text ?? string.Empty;
        var mnemonic = this.ItemData?.Mnemonic;

        this.textBlock.Inlines.Clear();

        if (string.IsNullOrEmpty(text))
        {
            return;
        }

        if (!showMnemonicUnderline || mnemonic is null)
        {
            this.textBlock.Inlines.Add(new Run { Text = text });
            return;
        }

        var matchIndex = text.IndexOf(mnemonic.Value.ToString(), System.StringComparison.CurrentCultureIgnoreCase);
        if (matchIndex < 0)
        {
            this.textBlock.Inlines.Add(new Run { Text = text });
            return;
        }

        if (matchIndex > 0)
        {
            this.textBlock.Inlines.Add(new Run { Text = text[..matchIndex] });
        }

        var underline = new Underline();
        underline.Inlines.Add(new Run { Text = text.Substring(matchIndex, 1) });
        this.textBlock.Inlines.Add(underline);

        if (matchIndex + 1 < text.Length)
        {
            this.textBlock.Inlines.Add(new Run { Text = text[(matchIndex + 1)..] });
        }
    }

    /// <summary>
    ///     Attempts to expand the submenu for this menu item if it has child items.
    /// </summary>
    /// <returns>
    ///     True if the submenu was requested to expand; otherwise, false.
    /// </returns>
    private bool TryExpandSubmenu()
    {
        if (this.ItemData is { HasChildren: true } data)
        {
            this.SubmenuRequested?.Invoke(this, new MenuItemSubmenuEventArgs { ItemData = data });
            return true;
        }

        return false;
    }

    /// <summary>
    ///     Attempts to invoke the menu item's command or selection logic if the item is enabled and does not have children.
    /// </summary>
    /// <returns>
    ///     True if the item was invoked or selection state was handled; false if invocation was not possible.
    /// </returns>
    private bool TryInvoke()
    {
        if (this.ItemData is not { IsEnabled: true, HasChildren: false } data)
        {
            return false;
        }

        if (data.Command is null)
        {
            // A command is not required; just handle selection state
            this.HandleSelectionState();
            return true;
        }

        try
        {
            // If the command cannot execute right now, indicate that no action occurred
            if (!data.Command.CanExecute(data))
            {
                return false;
            }

            data.Command.Execute(data);
            this.Invoked?.Invoke(this, new MenuItemInvokedEventArgs { ItemData = data });
            this.HandleSelectionState();
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            Debug.WriteLine($"Command execution failed: {ex}");
            this.CommandExecutionFailed?.Invoke(this, new MenuItemCommandFailedEventArgs
            {
                ItemData = data,
                Exception = ex,
            });
        }
#pragma warning restore CA1031

        // A command is attached to the menu item, and it was invoked
        // successfully or not.
        return true;
    }
}
