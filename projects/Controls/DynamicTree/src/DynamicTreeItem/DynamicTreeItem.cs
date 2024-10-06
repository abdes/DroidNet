// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.Specialized;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;

/// <summary>
/// Represents a dynamic tree item control that can be used within a hierarchical structure.
/// This control supports various visual states and template parts to manage its appearance and behavior.
/// </summary>
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = WithChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplateVisualState(Name = NoChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplatePart(Name = ThumbnailPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = ExpanderPart, Type = typeof(Expander))]
[TemplatePart(Name = ContentPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[TemplatePart(Name = BorderPart, Type = typeof(Border))]
[ContentProperty(Name = nameof(Content))]
public partial class DynamicTreeItem : ContentControl
{
    private const string ThumbnailPresenterPart = "PartThumbnailPresenter";
    private const string ExpanderPart = "PartExpander";
    private const string ContentPresenterPart = "PartContentPresenter";
    private const string RootGridPart = "PartRootGrid";
    private const string BorderPart = "PartBorder";

    private const string ExpansionVisualStates = "ExpansionStates";
    private const string ExpandedVisualState = "Expanded";
    private const string CollapsedVisualState = "Collapsed";

    private const string HasChildrenVisualStates = "HasChildrenStates";
    private const string WithChildrenVisualState = "WithChildren";
    private const string NoChildrenVisualState = "NoChildren";

    private const string CommonVisualStates = "CommonStates";
    private const string NormalVisualState = "Normal";
    private const string PointerOverVisualState = "PointerOver";
    private const string PointerOverSelectedVisualState = "PointerOverSelected";
    private const string SelectedVisualState = "Selected";

    /// <summary>
    /// Default indent increment value.
    /// </summary>
    private const double DefaultIndentIncrement = 34.0;

    private DynamicTree? treeControl;
    private Expander? expander;
    private long ancestorTreeThumbnailTemplateSelectorChangeCallbackToken;

    public DynamicTreeItem()
    {
        this.DefaultStyleKey = typeof(DynamicTreeItem);

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    protected override void OnApplyTemplate()
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

        this.OnThumbnailTemplateSelectorChanged();
        this.UpdateItemMargin();
        this.UpdateExpansionVisualState();
        this.UpdateHasChildrenVisualState();
        this.UpdateSelectionVisualState(this.ItemAdapter?.IsSelected ?? false);

        base.OnApplyTemplate();
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.PointerEntered += (_, _) =>
        {
            var isSelected = this.ItemAdapter?.IsSelected ?? false;
            VisualStateManager.GoToState(
                this,
                isSelected ? PointerOverSelectedVisualState : PointerOverVisualState,
                useTransitions: false);
        };

        this.PointerExited += (_, _) =>
        {
            var isSelected = this.ItemAdapter?.IsSelected ?? false;
            VisualStateManager.GoToState(
                this,
                isSelected ? SelectedVisualState : NormalVisualState,
                useTransitions: false);
        };

        // Get the parent tree control and update our properties and callbacks
        // for the first time when this control is loaded
        _ = this.FindParentTreeControl() ??
            throw new InvalidOperationException("DynamicTreeItem must be within a DynamicTree.");
        this.UpdateAncestorReference();

        // Subscribe to LayoutUpdated to detect parent changes
        this.LayoutUpdated += this.OnLayoutUpdated;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        // Detach event handlers to prevent memory leaks
        this.Loaded -= this.OnLoaded;
        this.Unloaded -= this.OnUnloaded;
        this.LayoutUpdated -= this.OnLayoutUpdated;
    }

    private void OnLayoutUpdated(object? sender, object args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.UpdateAncestorReference();
    }

    private void OnExpand(object? sender, EventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.Expand?.Invoke(
            this,
            new DynamicTreeEventArgs()
            {
                TreeItem = (TreeItemAdapter)this.DataContext,
            });
    }

    private void OnCollapse(object? sender, EventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.Collapse?.Invoke(
            this,
            new DynamicTreeEventArgs()
            {
                TreeItem = (TreeItemAdapter)this.DataContext,
            });
    }

    private void UpdateItemMargin()
    {
        if (this.ItemAdapter is null)
        {
            return;
        }

        // Try to get the indent increment from the XAML resources, fallback to default if not found
        var indentIncrement = DefaultIndentIncrement;
        if (Application.Current.Resources.TryGetValue(
                "DynamicTreeItemIndentIncrement",
                out var indentIncrementObj) && indentIncrementObj is double increment)
        {
            indentIncrement = increment;
        }

        // Calculate the extra left margin based on the IndentLevel
        var extraLeftMargin = this.ItemAdapter.Depth * indentIncrement;

        // Add the extra margin to the existing left margin
        if (this.GetTemplateChild(RootGridPart) is Grid rootGrid)
        {
            rootGrid.Margin = new Thickness(
                rootGrid.Margin.Left + extraLeftMargin,
                rootGrid.Margin.Top,
                rootGrid.Margin.Right,
                this.Margin.Bottom);
        }
    }

    private void UpdateSelectionVisualState(bool isSelected) => _ = VisualStateManager.GoToState(
        this,
        isSelected ? SelectedVisualState : NormalVisualState,
        useTransitions: true);

    private void UpdateExpansionVisualState()
    {
        if (this.expander is null)
        {
            return;
        }

        _ = VisualStateManager.GoToState(
            this,
            this.expander.IsExpanded ? ExpandedVisualState : CollapsedVisualState,
            useTransitions: true);
    }

    private void UpdateHasChildrenVisualState()
    {
        if (this.ItemAdapter is null)
        {
            return;
        }

        var childrenCount = this.ItemAdapter.ChildrenCount;
        var hasChildren = childrenCount > 0;
        _ = VisualStateManager.GoToState(
            this,
            hasChildren ? WithChildrenVisualState : NoChildrenVisualState,
            useTransitions: true);
    }

    private DynamicTree? FindParentTreeControl()
    {
        var parent = VisualTreeHelper.GetParent(this);

        while (parent is not null and not DynamicTree)
        {
            parent = VisualTreeHelper.GetParent(parent);
        }

        return parent as DynamicTree;
    }

    private void TreeItem_ChildrenCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.UpdateHasChildrenVisualState();
    }

    /// <summary>
    /// Updates the reference to the ancestor <see cref="DynamicTree" /> control. This method gets the thumbnail template selector
    /// from the ancestor with type <see cref="DynamicTree" /> and registers or unregisters property change callbacks as necessary
    /// to keep itself in-sync. This ensures that we handle properly the case where the item is being moved between two tree
    /// controls.
    /// </summary>
    /// <remarks>
    /// This method is called when the layout is updated which happens frequently. It's important to minimize the work it does.
    /// </remarks>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the <see cref="DynamicTreeItem" /> is not within a <see cref="DynamicTree" />.
    /// </exception>
    private void UpdateAncestorReference()
    {
        // Get the tree control ancestor amd bailout quickly if it did not change.
        var newAncestorTreeControl = this.FindParentTreeControl();
        if (this.treeControl == newAncestorTreeControl)
        {
            return;
        }

        // Unregister callbacks from the old tree control
        this.treeControl?.UnregisterPropertyChangedCallback(
            DynamicTree.ThumbnailTemplateSelectorProperty,
            this.ancestorTreeThumbnailTemplateSelectorChangeCallbackToken);

        this.treeControl = newAncestorTreeControl;
        if (this.treeControl == null)
        {
            return;
        }

        // Get the initial value of thumbnail template selector from our ancestor
        // tree control
        this.OnThumbnailTemplateSelectorChanged();

        // Register callbacks on the new tree control to get the updated template
        // selector when it changes
        this.ancestorTreeThumbnailTemplateSelectorChangeCallbackToken
            = this.treeControl.RegisterPropertyChangedCallback(
                DynamicTree.ThumbnailTemplateSelectorProperty,
                (_, _) => this.OnThumbnailTemplateSelectorChanged());
    }

    private void OnThumbnailTemplateSelectorChanged()
    {
        var templateSelector = this.treeControl?.ThumbnailTemplateSelector;
        if (templateSelector is null)
        {
            return;
        }

        if (this.GetTemplateChild(ThumbnailPresenterPart) is ContentPresenter { Content: Thumbnail thumbnail })
        {
            thumbnail.ContentTemplateSelector = templateSelector;
        }
    }
}
