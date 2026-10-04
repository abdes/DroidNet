// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Properties for the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="ThumbnailTemplateSelector" /> property.
    /// </summary>
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null, (d, _) => ((DynamicTree)d).UpdateThumbnailTemplateForRealizedItems()));

    /// <summary>Identifies the item-specific trailing-content template selector.</summary>
    public static readonly DependencyProperty TrailingContentTemplateSelectorProperty = DependencyProperty.Register(
        nameof(TrailingContentTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null, (d, _) => ((DynamicTree)d).UpdateTrailingContentForRealizedItems()));

    /// <summary>Identifies the shared width of the optional trailing-content column.</summary>
    public static readonly DependencyProperty TrailingContentWidthProperty = DependencyProperty.Register(
        nameof(TrailingContentWidth),
        typeof(double),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: 0d, (d, e) => ((DynamicTree)d).UpdateTrailingContentWidth((double)e.NewValue)));

    /// <summary>Identifies the interaction scope used by pointer/range selection.</summary>
    public static readonly DependencyProperty SelectionScopeProperty = DependencyProperty.Register(
        nameof(SelectionScope), typeof(TreeSelectionScope), typeof(DynamicTree), new PropertyMetadata(TreeSelectionScope.ShownItems));

    /// <summary>Identifies the minimum height of each rendered item row.</summary>
    public static readonly DependencyProperty ItemRowHeightProperty = DependencyProperty.Register(
        nameof(ItemRowHeight), typeof(double), typeof(DynamicTree), new PropertyMetadata(32d, (d, _) => ((DynamicTree)d).UpdateItemLayoutForRealizedItems()));

    /// <summary>Identifies the font size used by each rendered item label.</summary>
    public static readonly DependencyProperty ItemFontSizeProperty = DependencyProperty.Register(
        nameof(ItemFontSize), typeof(double), typeof(DynamicTree), new PropertyMetadata(14d, (d, _) => ((DynamicTree)d).UpdateItemLayoutForRealizedItems()));

    /// <summary>Identifies the size of the expander and thumbnail cells in a rendered item row.</summary>
    public static readonly DependencyProperty ItemIconSizeProperty = DependencyProperty.Register(
        nameof(ItemIconSize), typeof(double), typeof(DynamicTree), new PropertyMetadata(24d, (d, _) => ((DynamicTree)d).UpdateItemLayoutForRealizedItems()));

    /// <summary>Identifies the horizontal indentation applied for each tree depth level.</summary>
    public static readonly DependencyProperty ItemIndentWidthProperty = DependencyProperty.Register(
        nameof(ItemIndentWidth), typeof(double), typeof(DynamicTree), new PropertyMetadata(34d, (d, _) => ((DynamicTree)d).UpdateItemLayoutForRealizedItems()));

    /// <summary>Identifies the horizontal margin around expander and thumbnail cells.</summary>
    public static readonly DependencyProperty ItemIconMarginProperty = DependencyProperty.Register(
        nameof(ItemIconMargin), typeof(Thickness), typeof(DynamicTree), new PropertyMetadata(new Thickness(5, 0, 5, 0), (d, _) => ((DynamicTree)d).UpdateItemLayoutForRealizedItems()));

    /// <summary>
    ///     The backing <see cref="DependencyProperty"/> for the <see cref="IsFilteringEnabled"/> property.
    /// </summary>
    public static readonly DependencyProperty IsFilteringEnabledProperty = DependencyProperty.Register(
        nameof(IsFilteringEnabled),
        typeof(bool),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: false, (d, _) => ((DynamicTree)d).UpdateDisplayedItems()));

    /// <summary>
    ///     The backing <see cref="DependencyProperty"/> for the <see cref="DisplayedItems"/> property.
    /// </summary>
    public static readonly DependencyProperty DisplayedItemsProperty = DependencyProperty.Register(
        nameof(DisplayedItems),
        typeof(object),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Attached property storing the drop indicator position for an item container.
    /// </summary>
    public static readonly DependencyProperty DropIndicatorProperty = DependencyProperty.RegisterAttached(
        "DropIndicator",
        typeof(DropIndicatorPosition),
        typeof(DynamicTree),
        new PropertyMetadata(DropIndicatorPosition.None));

    /// <summary>
    /// Describes where a drop indicator should be shown for a tree item.
    /// </summary>
    public enum DropIndicatorPosition
    {
        /// <summary>
        /// No indicator is shown.
        /// </summary>
        None,

        /// <summary>
        /// Show an indicator before the item.
        /// </summary>
        Before,

        /// <summary>
        /// Show an indicator after the item.
        /// </summary>
        After,

        /// <summary>Show a highlighted target surface for dropping into the item.</summary>
        Inside,
    }

    /// <summary>
    ///     Gets or sets the data template selector for the thumbnails in the dynamic tree.
    /// </summary>
    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }

    /// <summary>Gets or sets the selector for optional generic content displayed after each item label.</summary>
    /// <remarks>A null selector leaves the trailing slot empty and consumes no width.</remarks>
    public DataTemplateSelector? TrailingContentTemplateSelector
    {
        get => (DataTemplateSelector?)this.GetValue(TrailingContentTemplateSelectorProperty);
        set => this.SetValue(TrailingContentTemplateSelectorProperty, value);
    }

    /// <summary>Gets or sets the optional aligned width, in DIPs, of the trailing-content column.</summary>
    /// <remarks>Zero uses content measurement and collapses the column when it has no template.</remarks>
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

    /// <summary>Gets or sets whether selection ranges refer to all expanded or only currently displayed items.</summary>
    /// <remarks>The default preserves historical ShownItems semantics for existing consumers.</remarks>
    public TreeSelectionScope SelectionScope
    {
        get => (TreeSelectionScope)this.GetValue(SelectionScopeProperty);
        set => this.SetValue(SelectionScopeProperty, value);
    }

    /// <summary>Gets or sets the minimum row height, in DIPs, used for tree items.</summary>
    /// <remarks>Consumers can vary this value to offer compact and comfortable presentation modes.</remarks>
    public double ItemRowHeight
    {
        get => (double)this.GetValue(ItemRowHeightProperty);
        set => this.SetValidatedPositiveDouble(ItemRowHeightProperty, value);
    }

    /// <summary>Gets or sets the font size, in DIPs, used for tree item labels.</summary>
    public double ItemFontSize
    {
        get => (double)this.GetValue(ItemFontSizeProperty);
        set => this.SetValidatedPositiveDouble(ItemFontSizeProperty, value);
    }

    /// <summary>Gets or sets the size, in DIPs, of the expander and thumbnail cells.</summary>
    public double ItemIconSize
    {
        get => (double)this.GetValue(ItemIconSizeProperty);
        set => this.SetValidatedPositiveDouble(ItemIconSizeProperty, value);
    }

    /// <summary>Gets or sets the indentation, in DIPs, applied for each level of tree depth.</summary>
    public double ItemIndentWidth
    {
        get => (double)this.GetValue(ItemIndentWidthProperty);
        set => this.SetValidatedPositiveDouble(ItemIndentWidthProperty, value);
    }

    /// <summary>Gets or sets the margin around each expander and thumbnail cell.</summary>
    public Thickness ItemIconMargin
    {
        get => (Thickness)this.GetValue(ItemIconMarginProperty);
        set => this.SetValue(ItemIconMarginProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the control renders <see cref="DynamicTreeViewModel.FilteredItems"/>
    ///     instead of <see cref="DynamicTreeViewModel.ShownItems"/>.
    /// </summary>
    public bool IsFilteringEnabled
    {
        get => (bool)this.GetValue(IsFilteringEnabledProperty);
        set => this.SetValue(IsFilteringEnabledProperty, value);
    }

    /// <summary>
    ///     Gets the items currently used as the ItemsRepeater source.
    /// </summary>
    public object? DisplayedItems
    {
        get => this.GetValue(DisplayedItemsProperty);
        private set => this.SetValue(DisplayedItemsProperty, value);
    }

    /// <summary>
    /// Gets the drop indicator position attached to the specified element.
    /// </summary>
    /// <param name="element">The element to query.</param>
    /// <returns>The current <see cref="DropIndicatorPosition"/>.</returns>
    public static DropIndicatorPosition GetDropIndicator(DependencyObject element)
        => (DropIndicatorPosition)element.GetValue(DropIndicatorProperty);

    /// <summary>
    /// Sets the drop indicator position on the specified element.
    /// </summary>
    /// <param name="element">The element to update.</param>
    /// <param name="value">The indicator position to set.</param>
    public static void SetDropIndicator(DependencyObject element, DropIndicatorPosition value)
        => element.SetValue(DropIndicatorProperty, value);

    private void SetValidatedPositiveDouble(DependencyProperty property, double value)
    {
        if (!double.IsFinite(value) || value <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }

        this.SetValue(property, value);
    }
}
