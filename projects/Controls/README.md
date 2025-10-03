# DroidNet Controls - WinUI 3 Custom Control Best Practices

This document outlines the best practices and patterns for implementing WinUI 3 custom controls based on the `DynamicTreeItem` control implementation.

## Table of Contents

1. [Project Structure and Code Organization](#project-structure-and-code-organization)
2. [Control Declaration and Attributes](#control-declaration-and-attributes)
3. [Partial Classes Pattern](#partial-classes-pattern)
4. [Dependency Properties](#dependency-properties)
5. [Template Parts](#template-parts)
6. [Visual States](#visual-states)
7. [Styling and Theming](#styling-and-theming)
8. [Event Handling](#event-handling)
9. [Memory Management](#memory-management)
10. [Template Processing](#template-processing)
11. [Documentation](#documentation)

## Project Structure and Code Organization

### Partial Classes Pattern

Split custom controls into logical partial classes for better maintainability:

```text
DynamicTreeItem/
├── DynamicTreeItem.cs              # Main control class with core logic
├── DynamicTreeItem.Properties.cs   # Dependency properties and property change handlers
├── DynamicTreeItem.Events.cs       # Event declarations and event handling
├── DynamicTreeItem.ItemRename.cs   # Feature-specific logic (e.g., in-place editing)
└── DynamicTreeItem.xaml            # Control template and styling
```

**Benefits:**

- Clear separation of concerns
- Easier code navigation and maintenance
- Reduced merge conflicts in team development
- Feature-specific logic isolation

## Control Declaration and Attributes

### Essential Attributes

Use descriptive attributes to define the control's contract:

```csharp
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = ExpandedVisualState, GroupName = ExpansionVisualStates)]
[TemplateVisualState(Name = CollapsedVisualState, GroupName = ExpansionVisualStates)]
[TemplatePart(Name = ThumbnailPresenterPart, Type = typeof(ContentPresenter))]
[TemplatePart(Name = ExpanderPart, Type = typeof(Expander))]
[TemplatePart(Name = ContentPresenterPart, Type = typeof(ContentPresenter))]
[ContentProperty(Name = nameof(Content))]
public partial class DynamicTreeItem : ContentControl
```

**Best Practices:**

- Document all visual states with `[TemplateVisualState]`
- Document all template parts with `[TemplatePart]` including expected types
- Specify content property with `[ContentProperty]` for XAML convenience
- Use meaningful, descriptive names that convey purpose

### Constants for Template Parts and States

Define constants for all template part names and visual states:

```csharp
// Template Part Names
private const string ThumbnailPresenterPart = "PartThumbnailPresenter";
private const string ExpanderPart = "PartExpander";
private const string ContentPresenterPart = "PartContentPresenter";

// Visual State Names
private const string NormalVisualState = "Normal";
private const string SelectedVisualState = "Selected";
private const string ExpandedVisualState = "Expanded";
private const string CollapsedVisualState = "Collapsed";

// Visual State Group Names
private const string CommonVisualStates = "CommonStates";
private const string ExpansionVisualStates = "ExpansionStates";
```

**Benefits:**

- Compile-time checking prevents typos
- IntelliSense support
- Easier refactoring
- Self-documenting code

## Dependency Properties

### Property Declaration Pattern

Follow this pattern for dependency properties:

```csharp
/// <summary>
/// The backing DependencyProperty for the ItemAdapter property.
/// </summary>
public static readonly DependencyProperty ItemAdapterProperty = DependencyProperty.Register(
    nameof(ItemAdapter),
    typeof(ITreeItem),
    typeof(DynamicTreeItem),
    new PropertyMetadata(
        defaultValue: null,
        (d, e) => ((DynamicTreeItem)d).OnItemAdapterChanged((ITreeItem)e.OldValue, (ITreeItem)e.NewValue)));

/// <summary>
/// Gets or sets the adapter that provides data for the tree item.
/// </summary>
/// <value>
/// An object that implements the ITreeItem interface, which provides data and
/// behavior for the tree item.
/// </value>
/// <remarks>
/// The ItemAdapter property is used to bind the tree item to a data source. When
/// the value of this property changes, the OnItemAdapterChanged method is called
/// to handle any necessary updates.
/// </remarks>
public ITreeItem? ItemAdapter
{
    get => (ITreeItem)GetValue(ItemAdapterProperty);
    set => SetValue(ItemAdapterProperty, value);
}
```

### Property Change Handlers

Implement dedicated change handlers with proper cleanup:

```csharp
/// <summary>
/// Handles changes to the ItemAdapter property.
/// </summary>
/// <param name="oldItem">The previous value of the ItemAdapter property.</param>
/// <param name="newItem">The new value of the ItemAdapter property.</param>
protected virtual void OnItemAdapterChanged(ITreeItem? oldItem, ITreeItem? newItem)
{
    // Un-register event handlers from the old item adapter if any
    if (oldItem is not null)
    {
        oldItem.ChildrenCollectionChanged -= TreeItem_ChildrenCollectionChanged;
        ((INotifyPropertyChanged)oldItem).PropertyChanged -= ItemAdapter_OnPropertyChanged;
    }

    // Update visual state and register new handlers
    if (newItem is not null)
    {
        UpdateExpansionVisualState();
        UpdateHasChildrenVisualState();
        UpdateSelectionVisualState(newItem.IsSelected);
        newItem.ChildrenCollectionChanged += TreeItem_ChildrenCollectionChanged;
        ((INotifyPropertyChanged)newItem).PropertyChanged += ItemAdapter_OnPropertyChanged;
    }
}
```

**Best Practices:**

- Always clean up old references to prevent memory leaks
- Use `nameof()` for property names to avoid typos
- Provide comprehensive XML documentation
- Make change handlers `protected virtual` to allow inheritance
- Update visual states when properties change

## Template Parts

### Template Part Access Pattern

Use safe template part access with null checking:

```csharp
protected override void OnApplyTemplate()
{
    // Validate required parts exist
    _ = GetTemplateChild(RootGridPart) as Grid ??
        throw new InvalidOperationException($"{nameof(DynamicTreeItem)} template is missing {RootGridPart}");

    SetupExpanderPart();
    SetupItemNameParts();

    UpdateItemMargin();
    UpdateExpansionVisualState();
    UpdateHasChildrenVisualState();

    base.OnApplyTemplate();
}

private void SetupExpanderPart()
{
    // Clean up old references
    if (expander is not null)
    {
        expander.Expand -= OnExpand;
        expander.Collapse -= OnCollapse;
    }

    // Get new part and setup handlers
    expander = GetTemplateChild(ExpanderPart) as Expander;
    if (expander is not null)
    {
        expander.Expand += OnExpand;
        expander.Collapse += OnCollapse;
    }
}
```

**Best Practices:**

- Always clean up event handlers from old template parts
- Validate critical template parts exist
- Use descriptive setup methods for complex parts
- Handle null template parts gracefully
- Call `base.OnApplyTemplate()` after your setup

## Visual States

### Visual State Management

Use descriptive visual state methods with consistent patterns:

```csharp
private void UpdateSelectionVisualState(bool isSelected)
    => VisualStateManager.GoToState(
        this,
        isSelected ? SelectedVisualState : NormalVisualState,
        useTransitions: true);

private void UpdateExpansionVisualState()
    => VisualStateManager.GoToState(
        this,
        ItemAdapter?.IsExpanded == true ? ExpandedVisualState : CollapsedVisualState,
        useTransitions: true);

private void UpdateHasChildrenVisualState()
    => VisualStateManager.GoToState(
        this,
        ItemAdapter?.ChildrenCount > 0 ? WithChildrenVisualState : NoChildrenVisualState,
        useTransitions: true);
```

### Visual State Groups in XAML

Organize visual states into logical groups:

```xaml
<VisualStateManager.VisualStateGroups>
    <VisualStateGroup x:Name="CommonStates">
        <VisualState x:Name="Normal">
            <VisualState.Setters>
                <Setter Target="PartRootGrid.Background" Value="{ThemeResource DynamicTreeItemBackground}" />
            </VisualState.Setters>
        </VisualState>
        <VisualState x:Name="PointerOver">
            <VisualState.Setters>
                <Setter Target="PartRootGrid.Background" Value="{ThemeResource DynamicTreeItemBackgroundPointerOver}" />
            </VisualState.Setters>
        </VisualState>
    </VisualStateGroup>

    <VisualStateGroup x:Name="ExpansionStates">
        <VisualState x:Name="Expanded" />
        <VisualState x:Name="Collapsed" />
    </VisualStateGroup>
</VisualStateManager.VisualStateGroups>
```

**Best Practices:**

- Group related states logically (CommonStates, ExpansionStates, etc.)
- Use transitions for smooth user experience
- Keep state update methods simple and focused
- Update states when template is applied and when properties change

## Styling and Theming

### Theme Resources Structure

Support multiple themes with comprehensive resource dictionaries:

```xaml
<ResourceDictionary.ThemeDictionaries>
    <ResourceDictionary x:Key="Default">
        <StaticResource x:Key="DynamicTreeItemBackground" ResourceKey="ListViewItemBackground" />
        <StaticResource x:Key="DynamicTreeItemBackgroundPointerOver" ResourceKey="ListViewItemBackgroundPointerOver" />
        <StaticResource x:Key="DynamicTreeItemBackgroundSelected" ResourceKey="ListViewItemBackgroundSelected" />
    </ResourceDictionary>
    <ResourceDictionary x:Key="Light">
        <StaticResource x:Key="DynamicTreeItemBackground" ResourceKey="ListViewItemBackground" />
        <StaticResource x:Key="DynamicTreeItemBackgroundPointerOver" ResourceKey="ListViewItemBackgroundPointerOver" />
        <StaticResource x:Key="DynamicTreeItemBackgroundSelected" ResourceKey="ListViewItemBackgroundSelected" />
    </ResourceDictionary>
    <ResourceDictionary x:Key="HighContrast">
        <StaticResource x:Key="DynamicTreeItemBackground" ResourceKey="SystemColorButtonFaceColor" />
        <StaticResource x:Key="DynamicTreeItemBackgroundPointerOver" ResourceKey="SystemColorHighlightColor" />
        <StaticResource x:Key="DynamicTreeItemBackgroundSelected" ResourceKey="SystemColorHighlightColor" />
    </ResourceDictionary>
</ResourceDictionary.ThemeDictionaries>
```

### Default Style Pattern

Provide a comprehensive default style:

```xaml
<Style x:Key="DefaultDynamicTreeItemStyle" TargetType="local:DynamicTreeItem">
    <Setter Property="Background" Value="{ThemeResource DynamicTreeItemBackground}" />
    <Setter Property="Template">
        <Setter.Value>
            <ControlTemplate TargetType="local:DynamicTreeItem">
                <!-- Template content -->
            </ControlTemplate>
        </Setter.Value>
    </Setter>
</Style>

<Style BasedOn="{StaticResource DefaultDynamicTreeItemStyle}" TargetType="local:DynamicTreeItem" />
```

**Best Practices:**

- Support Default, Light, and HighContrast themes
- Use semantic resource names that describe purpose
- Leverage existing system resources for consistency
- Provide both named and default styles
- Use merged dictionaries for shared resources

## Event Handling

### Event Declaration Pattern

Declare events with descriptive documentation:

```csharp
/// <summary>
/// Fires when the contents under the DynamicTreeItem need to be expanded.
/// </summary>
/// <remarks>
/// This event should be handled by the containing tree to expand the content under this
/// DynamicTreeItem.
/// </remarks>
public event EventHandler<DynamicTreeEventArgs>? Expand;

/// <summary>
/// Fires when the contents under the DynamicTreeItem need to be collapsed.
/// </summary>
/// <remarks>
/// This event should be handled by the containing tree to collapse the content under this
/// DynamicTreeItem.
/// </remarks>
public event EventHandler<DynamicTreeEventArgs>? Collapse;
```

### Event Args Pattern

Create specific event args classes:

```csharp
/// <summary>
/// Provides data for the Expand and Collapse events.
/// </summary>
public class DynamicTreeEventArgs : EventArgs
{
    /// <summary>
    /// Gets the tree item associated with the event.
    /// </summary>
    public required ITreeItem TreeItem { get; init; }
}
```

**Best Practices:**

- Use nullable event declarations (`event EventHandler<T>?`)
- Provide comprehensive XML documentation
- Create specific EventArgs classes with required data
- Use descriptive event names that indicate purpose
- Document when and why events fire

## Memory Management

### Lifecycle Event Handling

Properly handle control lifecycle to prevent memory leaks:

```csharp
public DynamicTreeItem()
{
    DefaultStyleKey = typeof(DynamicTreeItem);
    Loaded += OnLoaded;
    Unloaded += OnUnloaded;
}

private void OnLoaded(object sender, RoutedEventArgs args)
{
    // Setup event handlers that need the control to be loaded
    PointerEntered += (_, _) => { /* handler */ };
    PointerExited += (_, _) => { /* handler */ };
    LayoutUpdated += OnLayoutUpdated;
}

private void OnUnloaded(object sender, RoutedEventArgs e)
{
    // Clean up to prevent memory leaks
    Loaded -= OnLoaded;
    Unloaded -= OnUnloaded;
    LayoutUpdated -= OnLayoutUpdated;
}
```

### Property Change Callback Management

Manage property change callbacks properly:

```csharp
private void UpdateAncestorReference()
{
    var newAncestorTreeControl = FindParentTreeControl();
    if (treeControl == newAncestorTreeControl)
    {
        return; // No change, nothing to do
    }

    // Un-register callbacks from the old tree control
    treeControl?.UnregisterPropertyChangedCallback(
        DynamicTree.ThumbnailTemplateSelectorProperty,
        ancestorTreeThumbnailTemplateSelectorChangeCallbackToken);

    treeControl = newAncestorTreeControl;
    if (treeControl == null)
    {
        return;
    }

    // Register callbacks on the new tree control
    ancestorTreeThumbnailTemplateSelectorChangeCallbackToken
        = treeControl.RegisterPropertyChangedCallback(
            DynamicTree.ThumbnailTemplateSelectorProperty,
            (_, _) => OnThumbnailTemplateSelectorChanged());
}
```

**Best Practices:**

- Always unregister event handlers in `Unloaded`
- Clean up property change callbacks
- Use weak event patterns when appropriate
- Avoid circular references
- Store callback tokens for proper cleanup

## Template Processing

### Configuration Resource Pattern

Allow configuration through application resources:

```csharp
public DynamicTreeItem()
{
    DefaultStyleKey = typeof(DynamicTreeItem);

    // Try to get the indent increment from XAML resources, fallback to default
    indentIncrement = DefaultIndentIncrement;
    if (Application.Current.Resources.TryGetValue(
            "DynamicTreeItemIndentIncrement",
            out var indentIncrementObj) && indentIncrementObj is double increment)
    {
        indentIncrement = increment;
    }
}
```

### Template Part Validation

Validate critical template parts exist:

```csharp
protected override void OnApplyTemplate()
{
    _ = GetTemplateChild(RootGridPart) as Grid ??
        throw new InvalidOperationException($"{nameof(DynamicTreeItem)} template is missing {RootGridPart}");

    // Continue with template setup...
}
```

**Best Practices:**

- Allow customization through application resources
- Validate critical template parts exist
- Provide meaningful error messages for missing parts
- Use fallback values for optional configurations
- Update dependent calculations when template is applied

## Documentation

### XML Documentation Standards

Provide comprehensive XML documentation:

```csharp
/// <summary>
/// Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
/// expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
/// <remarks>
/// <para>
/// The DynamicTreeItem control is designed to be used within a DynamicTree control,
/// providing a versatile way to display items in a hierarchical tree where child items can be loaded on demand.
/// </para>
/// <para>
/// <strong>Usage Guidelines</strong>
/// </para>
/// <para>
/// To leverage the full capabilities of the DynamicTreeItem, use it within a DynamicTree control.
/// Implement data models that support on-demand loading of child items to optimize performance.
/// </para>
/// </remarks>
/// <example>
/// <![CDATA[
/// <!-- Use the DynamicTree in XAML -->
/// <controls:DynamicTree ItemsSource="{Binding RootItems}">
///     <controls:DynamicTree.ItemTemplate>
///         <DataTemplate>
///             <controls:DynamicTreeItem ItemAdapter="{Binding}"/>
///         </DataTemplate>
///     </controls:DynamicTree.ItemTemplate>
/// </controls:DynamicTree>
/// ]]>
/// </example>
```

**Best Practices:**

- Provide comprehensive class-level documentation
- Include usage guidelines and examples
- Document all public members with `<summary>`, `<param>`, and `<returns>`
- Use `<remarks>` for detailed explanations
- Include XAML examples where appropriate
- Use `<see cref="">` for cross-references

## Summary

Following these patterns ensures your WinUI 3 custom controls are:

- **Maintainable**: Clear code organization and separation of concerns
- **Extensible**: Proper use of virtual methods and inheritance
- **Themeable**: Comprehensive theme support and resource usage
- **Performant**: Proper memory management and lifecycle handling
- **Accessible**: Following WinUI standards and patterns
- **Discoverable**: Rich documentation and IntelliSense support

These practices, demonstrated in the `DynamicTreeItem` implementation, provide a solid foundation for building robust, professional-quality custom controls in WinUI 3.
