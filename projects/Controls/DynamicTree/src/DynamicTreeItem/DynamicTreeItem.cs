// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;

namespace DroidNet.Controls;

/// <summary>
/// Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
/// expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="DynamicTreeItem"/> control is designed to be used within a <see cref="DynamicTree"/> control,
/// providing a versatile way to display items in a hierarchical tree where child items can be loaded on demand.
/// This approach optimizes performance and resource usage, especially when dealing with large data sets or
/// complex hierarchies.
/// </para>
/// <para>
/// Users can interact with the tree by expanding and collapsing nodes to navigate through the hierarchy.
/// The control dynamically adjusts the visual representation, with indentation reflecting the depth of each item.
/// This hierarchical representation offers clear visual cues of parent-child relationships, enhancing navigability
/// within the tree.
/// </para>
/// <para>
/// Selection handling is a key feature of the <see cref="DynamicTreeItem"/>. Users can select items within the tree,
/// and the control updates its visual states to reflect the selection status. This is particularly useful in scenarios
/// where actions need to be performed on specific items, such as editing or deleting. Visual distinctions for selected
/// items help users keep track of their selections within potentially complex structures.
/// </para>
/// <para>
/// The control also supports in-place renaming, allowing users to rename items directly within the tree.
/// By double-clicking an item's label, users can initiate edit mode, making the item's name editable.
/// This streamlines the process of updating labels without the need for separate dialogs or forms.
/// The control includes validation mechanisms to ensure that new names meet the application's criteria,
/// providing immediate feedback if an entered name is invalid.
/// </para>
/// <para>
/// Customization is a significant aspect of the <see cref="DynamicTreeItem"/>. Developers can tailor the appearance
/// and behavior extensively through template parts and visual states. Template parts allow for customization
/// of specific components within the control, such as the expander icon or content presenter. Visual states enable
/// smooth transitions based on user interactions or data changes, enhancing the user experience.
/// </para>
/// <para>
/// <strong>Usage Guidelines</strong>
/// </para>
/// <para>
/// To leverage the full capabilities of the <see cref="DynamicTreeItem"/>, use it within a <see cref="DynamicTree"/> control.
/// Implement data models that support on-demand loading of child items to optimize performance, particularly
/// with large or complex hierarchies.
/// </para>
/// <para>
/// Customize the control to match your application's theme by redefining template parts. Adjust elements like colors,
/// fonts, and icons to create a cohesive user interface. Utilize visual states to provide feedback during user interactions,
/// such as hovering, selecting, or renaming items, resulting in a more responsive and intuitive experience.
/// </para>
/// <para>
/// To enable in-place renaming, handle the necessary events and implement validation logic to ensure item names
/// meet the application's requirements. Providing immediate feedback during renaming enhances the user experience
/// and helps prevent invalid data entries.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <![CDATA[
/// <!-- Define the data model -->
/// public class TreeItemViewModel : ITreeItem
/// {
///     public string Label { get; set; }
///     public ObservableCollection<ITreeItem> Children { get; set; }
///     // Implement other members of ITreeItem
/// }
///
/// <!-- Use the DynamicTree in XAML -->
/// <Page
///     x:Class="MyApp.MainPage"
///     xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
///     xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
///     xmlns:controls="using:DroidNet.Controls">
///
///     <Page.DataContext>
///         <local:MainViewModel />
///     </Page.DataContext>
///
///     <Grid>
///         <controls:DynamicTree ItemsSource="{Binding RootItems}">
///             <controls:DynamicTree.ItemTemplate>
///                 <DataTemplate>
///                     <controls:DynamicTreeItem ItemAdapter="{Binding}"/>
///                 </DataTemplate>
///             </controls:DynamicTree.ItemTemplate>
///         </controls:DynamicTree>
///     </Grid>
/// </Page>
///
/// <!-- ViewModel -->
/// public class MainViewModel
/// {
///     public ObservableCollection<ITreeItem> RootItems { get; set; }
///
///     public MainViewModel()
///     {
///         RootItems = new ObservableCollection<ITreeItem>
///         {
///             new TreeItemViewModel
///             {
///                 Label = "Root Item",
///                 Children = new ObservableCollection<ITreeItem>
///                 {
///                     new TreeItemViewModel { Label = "Child Item 1" },
///                     new TreeItemViewModel { Label = "Child Item 2" },
///                 }
///             }
///         };
///     }
/// }
/// ]]>
/// </example>
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = WithChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplateVisualState(Name = NoChildrenVisualState, GroupName = HasChildrenVisualStates)]
[TemplateVisualState(Name = NameIsValidVisualState, GroupName = NameValidationState)]
[TemplateVisualState(Name = NameIsInvalidVisualState, GroupName = NameValidationState)]
[TemplatePart(Name = ThumbnailPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = ExpanderPart, Type = typeof(Expander))]
[TemplatePart(Name = ContentPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = InPlaceRenamePart, Type = typeof(Popup))]
[TemplatePart(Name = ItemNamePart, Type = typeof(TextBlock))]
[TemplatePart(Name = ItemNameEditPart, Type = typeof(TextBox))]
[TemplatePart(Name = ContentGridPart, Type = typeof(Grid))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[ContentProperty(Name = nameof(Content))]
public partial class DynamicTreeItem : ContentControl
{
    private const string ThumbnailPresenterPart = "PartThumbnailPresenter";
    private const string ExpanderPart = "PartExpander";
    private const string RootGridPart = "PartRootGrid";
    private const string ContentGridPart = "PartContentGrid";
    private const string ContentPresenterPart = "PartContentPresenter";
    private const string InPlaceRenamePart = "PartInPlaceRename";
    private const string ItemNamePart = "PartItemName";
    private const string ItemNameEditPart = "PartItemNameEdit";

    private const string NameValidationState = "NameValidationStates";
    private const string NameIsValidVisualState = "NameIsValid";
    private const string NameIsInvalidVisualState = "NameIsInvalid";

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

    private readonly double indentIncrement;

    private DynamicTree? treeControl;
    private Expander? expander;

    private long ancestorTreeThumbnailTemplateSelectorChangeCallbackToken;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicTreeItem"/> class.
    /// </summary>
    public DynamicTreeItem()
    {
        this.DefaultStyleKey = typeof(DynamicTreeItem);

        // Try to get the indent increment from the XAML resources, fallback to default if not found
        this.indentIncrement = DefaultIndentIncrement;
        if (Application.Current.Resources.TryGetValue(
                "DynamicTreeItemIndentIncrement",
                out var indentIncrementObj) && indentIncrementObj is double increment)
        {
            this.indentIncrement = increment;
        }

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    /// <summary>
    /// Updates the margin of the item based on its depth in the tree.
    /// </summary>
    /// <remarks>
    /// Because of item recycling in the DynamicTree, the item's depth can change when it is reused
    /// to display a different item. Its margin is updated when its template is applied, but should
    /// also be updated every time it is prepared to be displayed (<see cref="ItemsRepeater.ElementPrepared"/>).)
    /// </remarks>
    internal void UpdateItemMargin()
    {
        // Handle situations when the item is not yet fully configured or its template not yet loaded
        if (this.ItemAdapter is null || this.GetTemplateChild(ContentGridPart) is not Grid rootGrid)
        {
            return;
        }

        // Calculate the extra left margin based on the IndentLevel and set it as the RootGrid margin
        var extraLeftMargin = this.ItemAdapter.Depth * this.indentIncrement;
        Debug.Assert(extraLeftMargin >= 0, "negative margin means bad depth, i.e. bug");
        rootGrid.Margin = new Thickness(extraLeftMargin, 0, 0, 0);
    }

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        _ = this.GetTemplateChild(RootGridPart) as Grid ?? throw new InvalidOperationException($"{nameof(DynamicTreeItem)} template is missing {RootGridPart}");

        this.SetupExpanderPart();
        this.SetupItemNameParts();

        this.OnThumbnailTemplateSelectorChanged();

        this.UpdateItemMargin();
        this.UpdateExpansionVisualState();
        this.UpdateHasChildrenVisualState();
        this.UpdateSelectionVisualState(this.ItemAdapter?.IsSelected == true);

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

        this.PointerEntered += (_, _) =>
        {
            var isSelected = this.ItemAdapter?.IsSelected ?? false;
            _ = VisualStateManager.GoToState(
                this,
                isSelected ? PointerOverSelectedVisualState : PointerOverVisualState,
                useTransitions: false);
        };

        this.PointerExited += (_, _) =>
        {
            var isSelected = this.ItemAdapter?.IsSelected ?? false;
            _ = VisualStateManager.GoToState(
                this,
                isSelected ? SelectedVisualState : NormalVisualState,
                useTransitions: false);
        };

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

    private void UpdateSelectionVisualState(bool isSelected)
        => VisualStateManager.GoToState(
            this,
            isSelected ? SelectedVisualState : NormalVisualState,
            useTransitions: true);

    private void UpdateExpansionVisualState()
        => VisualStateManager.GoToState(
            this,
            this.ItemAdapter?.IsExpanded == true ? ExpandedVisualState : CollapsedVisualState,
            useTransitions: true);

    private void UpdateHasChildrenVisualState()
        => VisualStateManager.GoToState(
            this,
            (this.ItemAdapter?.ChildrenCount > 0) ? WithChildrenVisualState : NoChildrenVisualState,
            useTransitions: true);

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

        _ = this.DispatcherQueue.TryEnqueue(this.UpdateHasChildrenVisualState);
    }

    /// <summary>
    /// Updates the reference to the ancestor <see cref="DynamicTree" /> control. This method gets
    /// the thumbnail template selector from the ancestor with type <see cref="DynamicTree" /> and
    /// registers or un-registers property change callbacks as necessary to keep itself in-sync.
    /// This ensures that we handle properly the case where the item is being moved between two tree
    /// controls.
    /// </summary>
    /// <remarks>
    /// This method is called when the layout is updated which happens frequently. It's important to
    /// minimize the work it does.
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
            // Tree control did not change or is null, nothing to do.
            return;
        }

        // Un-register callbacks from the old tree control
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
