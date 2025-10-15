// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or cascaded menu flyouts.
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
///         selection states, and disabled conditions. It handles the logic for command invocation and selection
///         (single item and radio group), but expansion decision and logic is left to the item containers.
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
[TemplateVisualState(Name = ActiveVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = CheckedNoIconVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = CheckedWithIconVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = WithChildrenVisualState, GroupName = DecorationVisualStates)]
[TemplateVisualState(Name = NoDecorationVisualState, GroupName = DecorationVisualStates)]
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
    ///     Visual state representing a menu item that is focused or expanded (when it has children).
    /// </summary>
    public const string ActiveVisualState = "Active";

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

    private bool isPressed;
    private bool isPointerOver;
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
        this.PointerCanceled += this.OnPointerCanceled;
        this.PointerCaptureLost += this.OnPointerCaptureLost;
        this.Tapped += this.OnTapped;

        this.AccessKeyDisplayRequested += this.OnAccessKeyDisplayRequested;
        this.AccessKeyDisplayDismissed += this.OnAccessKeyDisplayDismissed;

        this.Unloaded += (_, _) => this.ItemData?.PropertyChanged -= this.ItemData_OnPropertyChanged;
    }

    /// <summary>
    ///     Gets a value indicating whether the menu item participates in user interaction.
    /// </summary>
    /// <remarks>
    ///     True when the backing <c>ItemData</c> is non-null, the item is enabled and it is not rendered
    ///     as a separator. Non-interactive items are excluded from pointer/keyboard handling and visual
    ///     state transitions for interactive behavior.
    /// </remarks>
    internal bool IsInteractive => this.ItemData?.IsInteractive == true;

    /// <summary>
    ///     Gets a value indicating whether the control can receive keyboard focus.
    /// </summary>
    /// <remarks>
    ///     True when the item is interactive, marked as a tab stop and currently visible. This is used
    ///     by the focus and keyboard handlers to decide whether focus-related events and visual state
    ///     updates should be processed for this control.
    /// </remarks>
    internal bool IsFocusable =>
        this is { IsInteractive: true, IsTabStop: true, Visibility: Visibility.Visible };

    /// <summary>
    ///     Gets a value indicating whether the control template has been applied.
    /// </summary>
    /// <remarks>
    ///     When the template is applied, <see cref="rootGrid"/> is set to a non-null value.
    ///     This property is used to avoid duplicate visual state updates during initialization.
    /// </remarks>
    private bool IsTemplateApplied => this.rootGrid is not null;

    /// <summary>
    ///     Attempts to invoke the menu item's command or selection logic if the item is enabled and does not have children.
    /// </summary>
    /// <param name="inputSource">The <see cref="MenuInteractionInputSource"/> used to trigger this action.</param>
    internal void TryInvoke(MenuInteractionInputSource inputSource)
    {
        if (this.ItemData is not { IsInteractive: true, HasChildren: false } data)
        {
            return;
        }

        if (data.Command is null)
        {
            // A command is not required; just handle selection state
            this.HandleSelectionState();
            return;
        }

        try
        {
            // If the command cannot execute right now, indicate that no action occurred
            if (!data.Command.CanExecute(data))
            {
                return;
            }

            data.Command.Execute(data);
            this.Invoked?.Invoke(this, new MenuItemInvokedEventArgs
            {
                InputSource = inputSource,
                ItemData = data,
            });
            this.LogInvokedEvent(inputSource);
            this.HandleSelectionState();
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogCommandExecutionFailed(ex);
            this.Invoked?.Invoke(this, new MenuItemInvokedEventArgs
            {
                InputSource = inputSource,
                ItemData = data,
                Exception = new("Command failed with an error", ex),
            });
        }
#pragma warning restore CA1031
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

        if (this.ItemData is null)
        {
            return;
        }

        // Initialize visual states based on current ItemData
        this.UpdateTypeVisualState();
        this.UpdateCommonVisualState();
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
        Debug.Assert(this.IsInteractive, "Non-interactive menu items should not participate in focus");

        if (FocusManager.GetFocusedElement() is not FrameworkElement fe || !ReferenceEquals(fe, this))
        {
            // Focus was lost/stolen before the GotFocus event could be processed.
            return;
        }

        this.UpdateCommonVisualState();

        base.OnGotFocus(e);
        this.LogFocusState();
    }

    /// <summary>
    ///     Called when the control loses keyboard focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnLostFocus(RoutedEventArgs e)
    {
        Debug.Assert(this.IsInteractive, "Non-interactive menu items should not participate in focus");

        this.isPressed = false; // When the release occurs outside the control
        this.UpdateCommonVisualState();

        base.OnLostFocus(e);
        this.LogFocusState();
    }

    /// <summary>
    ///     Called when a key is pressed while the control has focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnKeyDown(KeyRoutedEventArgs e)
    {
        if (!this.IsInteractive)
        {
            base.OnKeyDown(e);
            return;
        }

        if (e.Key is VirtualKey.Enter or VirtualKey.Space)
        {
            this.LogKeyEvent(e.Key, "Down");
            this.isPressed = true;
            this.UpdateCommonVisualState();

            // We only handle invocation of the item here; expansion is handled by the container
            this.TryInvoke(MenuInteractionInputSource.KeyboardInput);
        }

        // Always invoke the base and let the event bubble up to allow menu containers
        // to handle navigation keys (arrows, Home/End, etc) and expansion.
        base.OnKeyDown(e);
    }

    /// <summary>
    ///     Called when a key is released while the control has focus.
    /// </summary>
    /// <param name="e">The event arguments.</param>
    protected override void OnKeyUp(KeyRoutedEventArgs e)
    {
        if (!this.IsInteractive)
        {
            base.OnKeyUp(e);
            return;
        }

        if (e.Key is VirtualKey.Enter or VirtualKey.Space)
        {
            this.LogKeyEvent(e.Key, "Up");
            this.isPressed = false;
            this.UpdateCommonVisualState();
        }

        base.OnKeyUp(e);
    }

    /// <summary>
    ///     Handles pointer enter events from the visual tree and updates hover state.
    ///     Raises <c>HoverStarted</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    private void OnPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Enter");

        this.isPointerOver = true;

        // Use Hand cursor for interactive menu items on hover
        this.ProtectedCursor = InputSystemCursor.Create(InputSystemCursorShape.Hand);
        this.UpdateCommonVisualState();
    }

    /// <summary>
    ///     Handles pointer exit events from the visual tree and clears hover state.
    ///     Raises <c>HoverEnded</c> (if applicable) and updates interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    private void OnPointerExited(object sender, PointerRoutedEventArgs e)
    {
        this.isPointerOver = false; // ensure cleared

        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Exit");

        this.UpdateCommonVisualState();

        Debug.Assert(this.ItemData is { }, "ItemData should be non-null");
    }

    /// <summary>
    ///     Handles pointer pressed events and updates the pressed visual state when appropriate.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Press");

        // Capture pointer so we reliably see pointer release/cancel even if pointer moves off the control.
        // Mark pressed even if capture fails, to ensure visual state is correct.
        _ = this.CapturePointer(e.Pointer);
        this.isPressed = true;
        _ = this.Focus(FocusState.Pointer);

        this.UpdateCommonVisualState();

    // _ = this.TryExpandSubmenu(MenuInteractionInputSource.PointerInput);
    }

    /// <summary>
    ///     Handles pointer release events and re-evaluates the interaction visual state.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Pointer event arguments.</param>
    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        this.isPressed = false; // ensure cleared
        this.ReleasePointerCaptures();

        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Release");

        // Ensure cursor is appropriate based on hover state after release
        this.ProtectedCursor = this.isPointerOver
            ? InputSystemCursor.Create(InputSystemCursorShape.Hand)
            : InputSystemCursor.Create(InputSystemCursorShape.Arrow);
        this.UpdateCommonVisualState();

        // The sender is the item over which the pointer was released, not the
        // one that captured it, and it should be `this` item.
        // Invoke it but only if it does not have Children, and the pointer is
        // still over it.
        if (this.ItemData is not { HasChildren: false })
        {
            return;
        }

        var position = e.GetCurrentPoint(this).Position;
        var bounds = new Rect(0, 0, this.ActualWidth, this.ActualHeight);
        if (!bounds.Contains(position))
        {
            return;
        }

        this.TryInvoke(MenuInteractionInputSource.PointerInput);
    }

    private void OnPointerCanceled(object sender, PointerRoutedEventArgs e)
    {
        this.isPressed = false; // ensure cleared
        this.ReleasePointerCaptures();

        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Cancel");

        // Reset to Arrow on cancel
        this.ProtectedCursor = InputSystemCursor.Create(InputSystemCursorShape.Arrow);
        this.UpdateCommonVisualState();
    }

    private void OnPointerCaptureLost(object sender, PointerRoutedEventArgs e)
    {
        this.isPressed = false; // ensure cleared

        if (!this.IsInteractive)
        {
            return;
        }

        this.LogPointerEvent("Capture Lost");

        // Reset to Arrow if capture is lost
        this.ProtectedCursor = InputSystemCursor.Create(InputSystemCursorShape.Arrow);
        this.UpdateCommonVisualState();
    }

    /// <summary>
    ///     Handles tapped events (activation). If the item has children, requests submenu
    ///     expansion; otherwise handles selection, executes any command, and raises
    ///     the <c>Invoked</c> event.
    /// </summary>
    /// <param name="sender">Event source (unused).</param>
    /// <param name="e">Tap event arguments (may be marked handled).</param>
    private void OnTapped(object sender, TappedRoutedEventArgs e)
    {
        if (!this.IsInteractive)
        {
            return;
        }

        this.LogTapped();

    // e.Handled = this.TryExpandOrInvoke(MenuInteractionInputSource.PointerInput);
    }

    private void UpdateTypeVisualState()
    {
        Debug.Assert(this.ItemData is { }, "Expecting a menu item with non-null ItemData");

        var isSeparator = this.ItemData?.IsSeparator ?? false;
        this.IsTabStop = !isSeparator;
        AutomationProperties.SetAccessibilityView(this, isSeparator ? AccessibilityView.Raw : AccessibilityView.Control);
        _ = VisualStateManager.GoToState(
                this,
                isSeparator ? SeparatorVisualState : ItemVisualState,
                useTransitions: true);
    }

    private void UpdateIconVisualState()
    {
        Debug.Assert(this.ItemData is { }, "Expecting a menu item with non-null ItemData");

        var state = this.ItemData.Icon is { } ? HasIconVisualState : NoIconVisualState;
        this.LogVisualState(state);
        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    private void UpdateAcceleratorVisualState()
    {
        Debug.Assert(this.ItemData is { }, "Expecting a menu item with non-null ItemData");

        var state = string.IsNullOrEmpty(this.ItemData.AcceleratorText) ? NoAcceleratorVisualState : HasAcceleratorVisualState;
        this.LogVisualState(state);
        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    private void UpdateCommonVisualState()
    {
        Debug.Assert(this.ItemData is { }, "Expecting a menu item with non-null ItemData");

        if (this.ItemData.IsSeparator)
        {
            this.isPointerOver = false;
            _ = VisualStateManager.GoToState(this, NormalVisualState, useTransitions: true);
            return;
        }

        var state = !this.ItemData.IsEnabled ? DisabledVisualState
                  : this.ItemData.IsExpanded ? ActiveVisualState
                  : this.isPressed ? PressedVisualState
                  : this.isPointerOver ? PointerOverVisualState
                  : NormalVisualState;

        this.LogVisualState(state);
        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    private void UpdateCheckmarkVisualState()
    {
        Debug.Assert(this.ItemData is { }, "Expecting a menu item with non-null ItemData");

        // Priority order: Submenu Arrow > Selection State > Nothing
        // Show checkmark on right side if item has icon, left side if no icon
        var data = this.ItemData;
        var state = (data.HasChildren && this.ShowSubmenuGlyph)
                ? WithChildrenVisualState
                : (data.HasSelectionState && data.IsChecked)
                    ? (data.Icon != null ? CheckedWithIconVisualState : CheckedNoIconVisualState)
                    : NoDecorationVisualState;
        this.LogVisualState(state);
        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }

    /// <summary>
    ///     Updates the visibility of the mnemonic underline programmatically.
    /// </summary>
    /// <param name="isVisible">True to show the mnemonic underline; false to hide it.</param>
    private void SetMnemonicVisibility(bool isVisible)
    {
        if (!this.IsInteractive)
        {
            return;
        }

        if (this.isMnemonicDisplayVisible == isVisible)
        {
            return;
        }

        this.LogMnemonicVisibility(this.isMnemonicDisplayVisible);

        this.isMnemonicDisplayVisible = isVisible;
        this.RefreshTextPresentation();
    }

    private void OnAccessKeyDisplayRequested(UIElement sender, AccessKeyDisplayRequestedEventArgs args)
        => this.SetMnemonicVisibility(isVisible: true);

    private void OnAccessKeyDisplayDismissed(UIElement sender, AccessKeyDisplayDismissedEventArgs args)
        => this.SetMnemonicVisibility(isVisible: false);

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
        Debug.Assert(this.ItemData is { }, "ItemData should be non-null");

        if (this.textBlock is null)
        {
            return;
        }

        var text = this.ItemData.Text ?? string.Empty;
        var mnemonic = this.ItemData.Mnemonic;

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

    private void HandleSelectionState()
    {
        Debug.Assert(this.ItemData is not null, "Item must have data, and should be checkable");

        var data = this.ItemData;

        // Only proceed for items that participate in selection state
        if (!data.HasSelectionState)
        {
            return;
        }

        // Radio group items delegate to MenuServices to ensure mutual exclusion.
        if (!string.IsNullOrEmpty(data.RadioGroupId))
        {
            if (this.MenuSource is { Services: { } services })
            {
                services.HandleGroupSelection(data);
            }

            // Without services, do not change selection locally; containers own group selection.
            this.LogRadioGroupSelection();
            return;
        }

        // Plain checkable items toggle their state locally
        data.IsChecked = !data.IsChecked;
        this.LogChecked();
    }
}
