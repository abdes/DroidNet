// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls;

/// <summary>
///     Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
///     expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
[SuppressMessage(
    "ReSharper",
    "ClassWithVirtualMembersNeverInherited.Global",
    Justification = "class is designed to be extended when needed")]
public partial class DynamicTreeItem
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="ItemAdapter" /> property.
    /// </summary>
    public static readonly DependencyProperty ItemAdapterProperty = DependencyProperty.Register(
        nameof(ItemAdapter),
        typeof(ITreeItem),
        typeof(DynamicTreeItem),
        new PropertyMetadata(
            defaultValue: null,
            (d, e) => ((DynamicTreeItem)d).OnItemAdapterChanged((ITreeItem)e.OldValue, (ITreeItem)e.NewValue)));

    /// <summary>
    ///     Identifies the <see cref="ILoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for NumberBox instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(ILoggerFactory),
        typeof(ILoggerFactory),
        typeof(DynamicTreeItem),
        new PropertyMetadata(defaultValue: null, (d, e) => ((DynamicTreeItem)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue)));

    /// <summary>Identifies the optional trailing-content object.</summary>
    public static readonly DependencyProperty TrailingContentProperty = DependencyProperty.Register(
        nameof(TrailingContent), typeof(object), typeof(DynamicTreeItem), new PropertyMetadata(defaultValue: null, (d, _) => ((DynamicTreeItem)d).UpdateTrailingContentPresenter()));

    /// <summary>Identifies the optional trailing-content template.</summary>
    public static readonly DependencyProperty TrailingContentTemplateProperty = DependencyProperty.Register(
        nameof(TrailingContentTemplate), typeof(DataTemplate), typeof(DynamicTreeItem), new PropertyMetadata(defaultValue: null, (d, _) => ((DynamicTreeItem)d).UpdateTrailingContentPresenter()));

    /// <summary>Identifies the optional shared trailing-content column width.</summary>
    public static readonly DependencyProperty TrailingContentWidthProperty = DependencyProperty.Register(
        nameof(TrailingContentWidth), typeof(double), typeof(DynamicTreeItem), new PropertyMetadata(defaultValue: 0d, (d, _) => ((DynamicTreeItem)d).UpdateTrailingContentPresenter()));

    /// <summary>Identifies the minimum height of the rendered item row.</summary>
    public static readonly DependencyProperty ItemRowHeightProperty = DependencyProperty.Register(
        nameof(ItemRowHeight), typeof(double), typeof(DynamicTreeItem), new PropertyMetadata(32d));

    /// <summary>Identifies the font size used for the rendered item label.</summary>
    public static readonly DependencyProperty ItemFontSizeProperty = DependencyProperty.Register(
        nameof(ItemFontSize), typeof(double), typeof(DynamicTreeItem), new PropertyMetadata(14d));

    /// <summary>Identifies the size of the expander and thumbnail cells.</summary>
    public static readonly DependencyProperty ItemIconSizeProperty = DependencyProperty.Register(
        nameof(ItemIconSize), typeof(double), typeof(DynamicTreeItem), new PropertyMetadata(24d));

    /// <summary>Identifies the indentation applied for each tree depth level.</summary>
    public static readonly DependencyProperty ItemIndentWidthProperty = DependencyProperty.Register(
        nameof(ItemIndentWidth), typeof(double), typeof(DynamicTreeItem), new PropertyMetadata(34d, (d, _) => ((DynamicTreeItem)d).OnItemIndentWidthChanged()));

    /// <summary>Identifies the margin around expander and thumbnail cells.</summary>
    public static readonly DependencyProperty ItemIconMarginProperty = DependencyProperty.Register(
        nameof(ItemIconMargin), typeof(Thickness), typeof(DynamicTreeItem), new PropertyMetadata(new Thickness(5, 0, 5, 0)));

    /// <summary>
    ///     Gets or sets the logical item that provides data and state for the tree item.
    /// </summary>
    /// <value>
    ///     An object that implements the <see cref="ITreeItem" /> interface.
    /// </value>
    /// <remarks>
    ///     The <see cref="ItemAdapter" /> property binds the visual row to a logical item. When
    ///     the value changes, the <see cref="OnItemAdapterChanged" /> method is called
    ///     to handle any necessary updates.
    /// </remarks>
    public ITreeItem? ItemAdapter
    {
        get => (ITreeItem?)this.GetValue(ItemAdapterProperty);
        set => this.SetValue(ItemAdapterProperty, value);
    }

    /// <summary>Gets or sets generic, consumer-owned content displayed after the item label.</summary>
    public object? TrailingContent { get => this.GetValue(TrailingContentProperty); set => this.SetValue(TrailingContentProperty, value); }

    /// <summary>Gets or sets the consumer-owned template for <see cref="TrailingContent"/>.</summary>
    public DataTemplate? TrailingContentTemplate { get => (DataTemplate?)this.GetValue(TrailingContentTemplateProperty); set => this.SetValue(TrailingContentTemplateProperty, value); }

    /// <summary>Gets or sets the optional aligned trailing-content width in DIPs.</summary>
    public double TrailingContentWidth
    {
        get => (double)this.GetValue(TrailingContentWidthProperty);
        set
        {
            if (!double.IsFinite(value) || value < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(value));
            }

            this.SetValue(TrailingContentWidthProperty, value);
        }
    }

    /// <summary>Gets or sets the minimum height, in DIPs, of this tree item row.</summary>
    public double ItemRowHeight { get => (double)this.GetValue(ItemRowHeightProperty); set => this.SetValue(ItemRowHeightProperty, value); }

    /// <summary>Gets or sets the font size, in DIPs, of this tree item label.</summary>
    public double ItemFontSize { get => (double)this.GetValue(ItemFontSizeProperty); set => this.SetValue(ItemFontSizeProperty, value); }

    /// <summary>Gets or sets the size, in DIPs, of the expander and thumbnail cells.</summary>
    public double ItemIconSize { get => (double)this.GetValue(ItemIconSizeProperty); set => this.SetValue(ItemIconSizeProperty, value); }

    /// <summary>Gets or sets the indentation, in DIPs, applied for each depth level.</summary>
    public double ItemIndentWidth { get => (double)this.GetValue(ItemIndentWidthProperty); set => this.SetValue(ItemIndentWidthProperty, value); }

    /// <summary>Gets or sets the margin around each expander and thumbnail cell.</summary>
    public Thickness ItemIconMargin { get => (Thickness)this.GetValue(ItemIconMarginProperty); set => this.SetValue(ItemIconMarginProperty, value); }

    /// <summary>
    ///     Gets or sets the <see cref="ILoggerFactory" /> used to create a logger for this control.
    ///     Assigning the factory will initialize the internal logger to a non-null logger instance
    ///     (falls back to <see cref="NullLoggerFactory.Instance"/> if null).
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>
    ///     Handles changes to the <see cref="ItemAdapter" /> property.
    /// </summary>
    /// <param name="oldItem">The previous value of the <see cref="ItemAdapter" /> property.</param>
    /// <param name="newItem">The new value of the <see cref="ItemAdapter" /> property.</param>
    /// <remarks>
    ///     This method is called whenever the <see cref="ItemAdapter" /> property changes. It
    ///     un-registers event handlers from the old item adapter and registers event handlers with the
    ///     new item adapter.
    /// </remarks>
    protected virtual void OnItemAdapterChanged(ITreeItem? oldItem, ITreeItem? newItem)
    {
        if (this.itemNameTextBox?.Visibility == Visibility.Visible)
        {
            this.CancelRename();
        }

        // Un-register event handlers from the old item adapter if any
        if (oldItem is not null)
        {
            oldItem.ChildrenCollectionChanged -= this.TreeItem_ChildrenCollectionChanged;
            if (oldItem is INotifyPropertyChanged oldNotifier)
            {
                oldNotifier.PropertyChanged -= this.ItemAdapter_OnPropertyChanged;
            }
        }

        // Update visual state based on the current value of IsSelected in the new TreeItem and
        // handle future changes to property values in the new TreeItem
        if (newItem is not null)
        {
            this.UpdateExpansionVisualState();
            this.UpdateHasChildrenVisualState();
            this.UpdateSelectionVisualState(newItem.IsSelected);
            this.UpdateCutVisualState(newItem.IsCut);
            newItem.ChildrenCollectionChanged += this.TreeItem_ChildrenCollectionChanged;
            if (newItem is INotifyPropertyChanged newNotifier)
            {
                newNotifier.PropertyChanged += this.ItemAdapter_OnPropertyChanged;
            }
        }
        else
        {
            this.UpdateCutVisualState(isCut: false);
        }

        this.OnTrailingContentTemplateSelectorChanged();
        this.UpdateTrailingContentPresenter();
        if (this.itemNameTextBlock is { } label)
        {
            label.SetBinding(TextBlock.TextProperty, new Binding
            {
                Source = newItem,
                Path = new PropertyPath(newItem is TreeItemAdapter ? nameof(TreeItemAdapter.DisplayLabel) : nameof(ITreeItem.Label)),
                Mode = BindingMode.OneWay,
            });
        }
    }

    private void ItemAdapter_OnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not ITreeItem adapter)
        {
            return;
        }

        if (string.Equals(args.PropertyName, nameof(ITreeItem.IsSelected), StringComparison.Ordinal))
        {
            this.UpdateSelectionVisualState(adapter.IsSelected);
        }
        else if (string.Equals(args.PropertyName, nameof(ITreeItem.IsCut), StringComparison.Ordinal))
        {
            this.UpdateCutVisualState(adapter.IsCut);
        }
        else if (string.Equals(args.PropertyName, nameof(ITreeItem.Depth), StringComparison.Ordinal))
        {
            this.UpdateItemMargin();
            this.UpdateTrailingContentPresenter();
        }
    }

    // Initialize the logger for this NumberBox. Use the NumberBox type as the category.
    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory) =>
        this.logger = loggerFactory?.CreateLogger<DynamicTreeItem>() ?? NullLoggerFactory.Instance.CreateLogger<DynamicTreeItem>();

    private void OnItemIndentWidthChanged()
    {
        this.UpdateItemMargin();
        this.UpdateTrailingContentPresenter();
    }
}
