// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using Windows.UI.Core;

namespace DroidNet.Controls;

/// <summary>
///     Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
///     expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
/// <remarks>
///     <para>
///         The <see cref="DynamicTreeItem" /> control is designed to be used within a <see cref="DynamicTree" /> control,
///         providing a versatile way to display items in a hierarchical tree where child items can be loaded on demand.
///         This approach optimizes performance and resource usage, especially when dealing with large data sets or
///         complex hierarchies.
///     </para>
///     <para>
///         Users can interact with the tree by expanding and collapsing nodes to navigate through the hierarchy.
///         The control dynamically adjusts the visual representation, with indentation reflecting the depth of each item.
///         This hierarchical representation offers clear visual cues of parent-child relationships, enhancing navigability
///         within the tree.
///     </para>
///     <para>
///         Selection handling is a key feature of the <see cref="DynamicTreeItem" />. Users can select items within the
///         tree,
///         and the control updates its visual states to reflect the selection status. This is particularly useful in
///         scenarios
///         where actions need to be performed on specific items, such as editing or deleting. Visual distinctions for
///         selected
///         items help users keep track of their selections within potentially complex structures.
///     </para>
///     <para>
///         The control also supports in-place renaming, allowing users to rename items directly within the tree.
///         By double-clicking an item's label, users can initiate edit mode, making the item's name editable.
///         This streamlines the process of updating labels without the need for separate dialogs or forms.
///         The control includes validation mechanisms to ensure that new names meet the application's criteria,
///         providing immediate feedback if an entered name is invalid.
///     </para>
///     <para>
///         Customization is a significant aspect of the <see cref="DynamicTreeItem" />. Developers can tailor the
///         appearance
///         and behavior extensively through template parts and visual states. Template parts allow for customization
///         of specific components within the control, such as the expander icon or content presenter. Visual states enable
///         smooth transitions based on user interactions or data changes, enhancing the user experience.
///     </para>
///     <para>
///         <strong>Usage Guidelines</strong>
///     </para>
///     <para>
///         To leverage the full capabilities of the <see cref="DynamicTreeItem" />, use it within a
///         <see cref="DynamicTree" /> control.
///         Implement data models that support on-demand loading of child items to optimize performance, particularly
///         with large or complex hierarchies.
///     </para>
///     <para>
///         Customize the control to match your application's theme by redefining template parts. Adjust elements like
///         colors,
///         fonts, and icons to create a cohesive user interface. Utilize visual states to provide feedback during user
///         interactions,
///         such as hovering, selecting, or renaming items, resulting in a more responsive and intuitive experience.
///     </para>
///     <para>
///         To enable in-place renaming, handle the necessary events and implement validation logic to ensure item names
///         meet the application's requirements. Providing immediate feedback during renaming enhances the user experience
///         and helps prevent invalid data entries.
///     </para>
/// </remarks>
/// <example>
///     <para>
///         <strong>Example Usage</strong>
///     </para>
///     <para>
///         Rows are created by <see cref="DynamicTree" />: it realizes one <see cref="DynamicTreeItem" /> per displayed
///         item from its control template and binds <see cref="ItemAdapter" /> to the logical item. Applications do not
///         create rows; they supply the logical items through the tree's view model and style the rows through the
///         template parts and visual states documented on this type.
///     </para>
///     <![CDATA[
/// <controls:DynamicTree ViewModel="{x:Bind ViewModel}" ItemRowHeight="28" />
/// ]]>
/// </example>
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = CutVisualState, GroupName = CutVisualStates)]
[TemplateVisualState(Name = NotCutVisualState, GroupName = CutVisualStates)]
[TemplateVisualState(Name = DimmedVisualState, GroupName = DimVisualStates)]
[TemplateVisualState(Name = NotDimmedVisualState, GroupName = DimVisualStates)]
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = WithChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplateVisualState(Name = NoChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplateVisualState(Name = NameIsValidVisualState, GroupName = NameValidationState)]
[TemplateVisualState(Name = NameIsInvalidVisualState, GroupName = NameValidationState)]
[TemplatePart(Name = ThumbnailPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = TrailingContentPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = ExpanderPart, Type = typeof(Expander))]
[TemplatePart(Name = ContentPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = InPlaceRenamePart, Type = typeof(Popup))]
[TemplatePart(Name = ItemNamePart, Type = typeof(TextBlock))]
[TemplatePart(Name = ItemNameEditPart, Type = typeof(TextBox))]
[TemplatePart(Name = ItemNameErrorPart, Type = typeof(FontIcon))]
[TemplatePart(Name = ContentGridPart, Type = typeof(Grid))]
[TemplatePart(Name = DropIntoIndicatorPart, Type = typeof(Border))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
public partial class DynamicTreeItem : Control
{
    /// <summary>
    /// The name of the thumbnail presenter part used to host thumbnails inside the item template.
    /// </summary>
    public const string ThumbnailPresenterPart = "PartThumbnailPresenter";

    /// <summary>The template part presenting optional consumer-owned trailing content.</summary>
    public const string TrailingContentPresenterPart = "PartTrailingContentPresenter";

    /// <summary>The template part highlighted when an item is a drop-into target.</summary>
    public const string DropIntoIndicatorPart = "PartDropIntoIndicator";

    /// <summary>
    /// The name of the expander control part inside the tree item template.
    /// </summary>
    public const string ExpanderPart = "PartExpander";

    /// <summary>
    /// The name of the root grid part in the tree item template.
    /// </summary>
    public const string RootGridPart = "PartRootGrid";

    /// <summary>
    /// The name of the content grid that wraps content inside a tree item template.
    /// </summary>
    public const string ContentGridPart = "PartContentGrid";

    /// <summary>
    /// The name of the content presenter part inside the tree item template.
    /// </summary>
    public const string ContentPresenterPart = "PartContentPresenter";

    /// <summary>
    /// The name of the popup that hosts in-place rename UI inside the tree item template.
    /// </summary>
    public const string InPlaceRenamePart = "PartInPlaceRename";

    /// <summary>
    /// The name of the text block that displays the item name inside the tree item template.
    /// </summary>
    /// <remarks>
    /// When the text block's inlines are exactly two <see cref="Microsoft.UI.Xaml.Documents.Run"/> elements, the row writes
    /// the item's label to the first and its annotation (in secondary text) to the second. Otherwise the row binds the
    /// text block's text to the item's label.
    /// </remarks>
    public const string ItemNamePart = "PartItemName";

    /// <summary>
    /// The name of the text box used for editing the item's name in the template.
    /// </summary>
    public const string ItemNameEditPart = "PartItemNameEdit";

    /// <summary>The name of the glyph shown when the proposed rename is invalid or rejected.</summary>
    public const string ItemNameErrorPart = "PartItemNameError";

    /// <summary>
    /// The name of the VisualStateGroup used for name validation states.
    /// </summary>
    public const string NameValidationState = "NameValidationStates";

    /// <summary>
    /// Visual state name used when the item's name is valid.
    /// </summary>
    public const string NameIsValidVisualState = "NameIsValid";

    /// <summary>
    /// Visual state name used when the item's name is invalid.
    /// </summary>
    public const string NameIsInvalidVisualState = "NameIsInvalid";

    /// <summary>
    /// The name of the VisualStateGroup controlling expand/collapse states.
    /// </summary>
    public const string ExpansionVisualStates = "ExpansionStates";

    /// <summary>
    /// Visual state name used when the item is expanded.
    /// </summary>
    public const string ExpandedVisualState = "Expanded";

    /// <summary>
    /// Visual state name used when the item is collapsed.
    /// </summary>
    public const string CollapsedVisualState = "Collapsed";

    /// <summary>
    /// The name of the VisualStateGroup that indicates whether the item has children.
    /// </summary>
    public const string HasChildrenVisualStates = "HasChildrenStates";

    /// <summary>
    /// Visual state name used when the item has children.
    /// </summary>
    public const string WithChildrenVisualState = "WithChildren";

    /// <summary>
    /// Visual state name used when the item does not have children.
    /// </summary>
    public const string NoChildrenVisualState = "NoChildren";

    /// <summary>
    /// The canonical name of the VisualStateGroup that contains common selection states.
    /// </summary>
    public const string CommonVisualStates = "CommonStates";

    /// <summary>
    /// Visual state name used for a normal (unselected) item.
    /// </summary>
    public const string NormalVisualState = "Normal";

    /// <summary>
    /// Visual state name used for pointer over state when item is not selected.
    /// </summary>
    public const string PointerOverVisualState = "PointerOver";

    /// <summary>
    /// Visual state name used for pointer over state when item is already selected.
    /// </summary>
    public const string PointerOverSelectedVisualState = "PointerOverSelected";

    /// <summary>
    /// Visual state name used for a selected item.
    /// </summary>
    public const string SelectedVisualState = "Selected";

    /// <summary>
    /// The name of the VisualStateGroup that indicates cut status.
    /// </summary>
    public const string CutVisualStates = "CutStates";

    /// <summary>
    /// Visual state name used when the item is marked as cut.
    /// </summary>
    public const string CutVisualState = "Cut";

    /// <summary>
    /// Visual state name used when the item is not cut.
    /// </summary>
    public const string NotCutVisualState = "NotCut";

    /// <summary>
    /// The name of the VisualStateGroup that indicates whether the item is dimmed (<see cref="TreeItemAdapter.IsDimmed"/>).
    /// </summary>
    public const string DimVisualStates = "DimStates";

    /// <summary>
    /// Visual state name used when the item's name and glyph draw in secondary text.
    /// </summary>
    public const string DimmedVisualState = "Dimmed";

    /// <summary>
    /// Visual state name used when the item draws in primary text.
    /// </summary>
    public const string NotDimmedVisualState = "NotDimmed";

    private ILogger logger = NullLogger<DynamicTreeItem>.Instance;

    private Expander? expander;
    private ColumnDefinition? trailingContentColumn;
    private ContentPresenter? trailingContentPresenter;
    private Border? dropIntoIndicator;
    private bool isPointerHandlersAttached;
    private bool isInteractivePointerActive;
    private bool areInteractivePointerHandlersAttached;
    private bool isApplyingTrailingContent;
    private bool hasChildrenUpdatePending;

    // The row follows its logical item only while it is realized and loaded: a recycled or unloaded row must not stay
    // subscribed to the item it last showed.
    private bool isObservationSuspended;

    // A detached row waits in the repeater's pool: loading it again must not resume following the item it last showed.
    private bool isDetached;
    private ITreeItem? observedItem;

    private DynamicTree? treeControl;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DynamicTreeItem" /> class.
    /// </summary>
    public DynamicTreeItem()
    {
        this.DefaultStyleKey = typeof(DynamicTreeItem);

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    /// <summary>Gets the tree this row is realized by, or <see langword="null"/> when it is not realized by a tree.</summary>
    internal DynamicTree? OwnerTree => this.treeControl;

    /// <summary>
    ///     Updates the margin of the item based on its depth in the tree.
    /// </summary>
    /// <remarks>
    ///     Because of item recycling in the DynamicTree, the item's depth can change when it is reused
    ///     to display a different item. Its margin is updated when its template is applied, but should
    ///     also be updated every time it is prepared to be displayed (<see cref="ItemsRepeater.ElementPrepared" />).
    /// </remarks>
    internal void UpdateItemMargin()
    {
        // Handle situations when the item is not yet fully configured or its template not yet loaded
        if (this.ItemAdapter is null || this.GetTemplateChild(ContentGridPart) is not Grid rootGrid)
        {
            return;
        }

        // Removed items have an unassigned (negative) depth before their realized rows are recycled.
        var extraLeftMargin = Math.Max(0, this.ItemAdapter.Depth) * this.ItemIndentWidth;
        rootGrid.Margin = new Thickness(extraLeftMargin, 0, 0, 0);
        this.LogItemMarginUpdated(extraLeftMargin);
    }

    /// <summary>
    ///     Binds the row to the tree that realizes it. The tree calls this when the row is prepared or recycled to a new
    ///     item, so the row never searches the visual tree for its owner.
    /// </summary>
    /// <param name="tree">The tree realizing this row.</param>
    internal void AttachTo(DynamicTree tree)
    {
        ArgumentNullException.ThrowIfNull(tree);

        this.treeControl = tree;
        this.isDetached = false;
        this.isObservationSuspended = false;
        this.ObserveItem(this.ItemAdapter);
        this.RefreshItemPresentation();
        this.OnThumbnailTemplateSelectorChanged();
        this.UpdateTrailingContentFromTree();
        this.UpdateItemMargin();
    }

    /// <summary>
    ///     Releases the row from the tree and from its logical item when the tree recycles it, so the pooled row is
    ///     neither kept alive by, nor reacting to, the item it last showed.
    /// </summary>
    internal void Detach()
    {
        this.isDetached = true;
        this.isObservationSuspended = true;
        this.ObserveItem(item: null);
        this.treeControl = null;
        this.UpdateTrailingContentTemplate(selector: null, item: null);
        this.ResetRecycledRowState();
    }

    /// <summary>
    ///     Clears the per-row interaction presentation so a recycled container carries nothing from
    ///     the row it used to display.
    /// </summary>
    /// <remarks>
    ///     <see cref="ItemsRepeater"/> recycles element containers, and a container that is cleared
    ///     mid-press, mid-rename or while it carries the drop cue would otherwise hand that state to
    ///     the next row it is bound to: a stale interactive flag makes the control cancel drags
    ///     started on that row, and a stale cue points the user at the wrong drop position.
    /// </remarks>
    internal void ResetRecycledRowState()
    {
        this.isInteractivePointerActive = false;
        this.UpdateDropIndicatorVisual(DynamicTree.DropIndicatorPosition.None);
        if (this.itemNameTextBox?.Visibility == Visibility.Visible)
        {
            this.CancelRename();
        }
    }

    /// <summary>Pushes the tree's thumbnail template selector onto this row's thumbnail.</summary>
    /// <remarks>
    ///     The thumbnail selects its own template again whenever its content or selector changes, so recycled rows
    ///     need no further handling.
    /// </remarks>
    internal void OnThumbnailTemplateSelectorChanged()
    {
        if (this.treeControl is not { } tree
            || this.GetTemplateChild(ThumbnailPresenterPart) is not ContentPresenter { Content: Thumbnail thumbnail })
        {
            return;
        }

        // A tree that drops its selector gives the thumbnail its own default back.
        if (tree.ThumbnailTemplateSelector is { } templateSelector)
        {
            thumbnail.ContentTemplateSelector = templateSelector;
        }
        else
        {
            thumbnail.ClearValue(ContentControl.ContentTemplateSelectorProperty);
        }
    }

    /// <summary>Applies the tree's trailing-content template, selector and width to this row.</summary>
    internal void UpdateTrailingContentFromTree()
    {
        var tree = this.treeControl;
        this.UpdateTrailingContentTemplate(tree?.TrailingContentTemplateSelector, this.ItemAdapter, tree?.TrailingContentTemplate);
        if (tree is not null)
        {
            this.UpdateTrailingContentWidth(tree.EffectiveTrailingContentWidth);
        }
    }

    /// <summary>Requests expansion or collapse on behalf of an automation client.</summary>
    /// <param name="expand">Whether to expand; otherwise collapse.</param>
    internal void RequestExpansion(bool expand)
    {
        if (expand)
        {
            this.OnExpand(this, EventArgs.Empty);
        }
        else
        {
            this.OnCollapse(this, EventArgs.Empty);
        }
    }

    /// <summary>Requests selection of this row on behalf of an automation client.</summary>
    /// <param name="toggle">Whether to toggle the row in the current selection; otherwise select only this row.</param>
    internal void RequestSelection(bool toggle)
    {
        if (this.treeControl is { } tree && this.ItemAdapter is { } item)
        {
            tree.SelectFromAutomation(item, toggle);
        }
    }

    /// <inheritdoc />
    protected override AutomationPeer OnCreateAutomationPeer() => new DynamicTreeItemAutomationPeer(this);

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        _ = this.GetTemplateChild(RootGridPart) as Grid ??
            throw new InvalidOperationException($"{nameof(DynamicTreeItem)} template is missing {RootGridPart}");

        this.SetupExpanderPart();
        this.SetupItemNameParts();

        this.OnThumbnailTemplateSelectorChanged();
        this.trailingContentPresenter = this.GetTemplateChild(TrailingContentPresenterPart) as ContentPresenter;
        this.trailingContentColumn = (this.GetTemplateChild(RootGridPart) as Grid)?.ColumnDefinitions.ElementAtOrDefault(1);
        this.dropIntoIndicator = this.GetTemplateChild(DropIntoIndicatorPart) as Border;
        this.UpdateTrailingContentFromTree();
        this.UpdateItemMargin();
        this.RefreshItemPresentation();

        base.OnApplyTemplate();
    }

    private static bool IsShiftKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Shift)
        .HasFlag(CoreVirtualKeyStates.Down);

    private void SetupExpanderPart()
    {
        if (this.expander is not null)
        {
            this.expander.Expand -= this.OnExpand;
            this.expander.Collapse -= this.OnCollapse;
        }

        this.expander = this.GetTemplateChild(ExpanderPart) as Expander;

        if (this.expander is not null)
        {
            this.expander.Expand += this.OnExpand;
            this.expander.Collapse += this.OnCollapse;
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (!this.isPointerHandlersAttached)
        {
            this.PointerEntered += this.OnPointerEntered;
            this.PointerExited += this.OnPointerExited;
            this.isPointerHandlersAttached = true;
        }

        if (!this.areInteractivePointerHandlersAttached)
        {
            this.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler(this.OnActionPointerPressed), handledEventsToo: true);
            this.AddHandler(UIElement.PointerReleasedEvent, new PointerEventHandler(this.OnActionPointerEnded), handledEventsToo: true);
            this.AddHandler(UIElement.PointerCanceledEvent, new PointerEventHandler(this.OnActionPointerEnded), handledEventsToo: true);
            this.AddHandler(UIElement.PointerCaptureLostEvent, new PointerEventHandler(this.OnActionPointerEnded), handledEventsToo: true);
            this.areInteractivePointerHandlersAttached = true;
        }

        if (!this.isDetached)
        {
            this.SetObservationSuspended(suspended: false);
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        if (this.isPointerHandlersAttached)
        {
            this.PointerEntered -= this.OnPointerEntered;
            this.PointerExited -= this.OnPointerExited;
            this.isPointerHandlersAttached = false;
        }

        if (this.areInteractivePointerHandlersAttached)
        {
            this.RemoveHandler(UIElement.PointerPressedEvent, new PointerEventHandler(this.OnActionPointerPressed));
            this.RemoveHandler(UIElement.PointerReleasedEvent, new PointerEventHandler(this.OnActionPointerEnded));
            this.RemoveHandler(UIElement.PointerCanceledEvent, new PointerEventHandler(this.OnActionPointerEnded));
            this.RemoveHandler(UIElement.PointerCaptureLostEvent, new PointerEventHandler(this.OnActionPointerEnded));
            this.areInteractivePointerHandlersAttached = false;
        }

        this.ResetRecycledRowState();
        this.SetObservationSuspended(suspended: true);
    }

    private void OnPointerEntered(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        var isSelected = this.ItemAdapter?.IsSelected ?? false;
        _ = VisualStateManager.GoToState(this, isSelected ? PointerOverSelectedVisualState : PointerOverVisualState, useTransitions: false);
    }

    private void OnPointerExited(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        var isSelected = this.ItemAdapter?.IsSelected ?? false;
        _ = VisualStateManager.GoToState(this, isSelected ? SelectedVisualState : NormalVisualState, useTransitions: false);
    }

    private void OnActionPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        if (this.IsInteractiveContentElement(args.OriginalSource as DependencyObject))
        {
            this.isInteractivePointerActive = true;
        }
    }

    private void OnActionPointerEnded(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.isInteractivePointerActive = false;
    }

    private void OnExpand(object? sender, EventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ItemAdapter is { } item)
        {
            this.Expand?.Invoke(this, new DynamicTreeEventArgs { TreeItem = item });
        }
    }

    private void OnCollapse(object? sender, EventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ItemAdapter is { } item)
        {
            this.Collapse?.Invoke(this, new DynamicTreeEventArgs { TreeItem = item });
        }
    }

    private void UpdateSelectionVisualState(bool isSelected)
        => VisualStateManager.GoToState(
            this,
            isSelected ? SelectedVisualState : NormalVisualState,
            useTransitions: true);

    private void UpdateCutVisualState(bool isCut)
        => VisualStateManager.GoToState(
            this,
            isCut ? CutVisualState : NotCutVisualState,
            useTransitions: true);

    private void UpdateDimVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemAdapter is TreeItemAdapter { IsDimmed: true } ? DimmedVisualState : NotDimmedVisualState,
            useTransitions: false);

    private void UpdateExpansionVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemAdapter?.IsExpanded == true ? ExpandedVisualState : CollapsedVisualState,
            useTransitions: true);

    private void UpdateHasChildrenVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemAdapter?.ChildrenCount > 0 ? WithChildrenVisualState : NoChildrenVisualState,
            useTransitions: true);

    private void SetObservationSuspended(bool suspended)
    {
        if (this.isObservationSuspended == suspended)
        {
            return;
        }

        this.isObservationSuspended = suspended;
        this.ObserveItem(suspended ? null : this.ItemAdapter);
        if (!suspended)
        {
            this.RefreshItemPresentation();
        }
    }

    // Brings every item-driven visual state and the label up to date with the current item.
    private void RefreshItemPresentation()
    {
        this.UpdateExpansionVisualState();
        this.UpdateHasChildrenVisualState();
        this.UpdateSelectionVisualState(this.ItemAdapter?.IsSelected == true);
        this.UpdateCutVisualState(this.ItemAdapter?.IsCut == true);
        this.UpdateDimVisualState();
        this.BindItemName();
    }
}
