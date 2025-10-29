// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = IconPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = HeaderPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ButtonsContainerPartName, Type = typeof(StackPanel))]
[TemplatePart(Name = PinButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = CloseButtonPartName, Type = typeof(Button))]
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = PointerOverVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedPointerOverVisualState, GroupName = CommonVisualStates)]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]

public partial class TabStripItem : ContentControl
{
    /// <summary>The name of the root grid template part.</summary>
    public const string RootGridPartName = "PartRootGrid";

    /// <summary>The name of the icon template part.</summary>
    public const string IconPartName = "PartIcon";

    /// <summary>The name of the header template part.</summary>
    public const string HeaderPartName = "PartHeader";

    /// <summary>The name of the buttons container template part.</summary>
    public const string ButtonsContainerPartName = "ButtonsContainer";

    /// <summary>The name of the pin button template part.</summary>
    public const string PinButtonPartName = "PartPinButton";

    /// <summary>The name of the close button template part.</summary>
    public const string CloseButtonPartName = "PartCloseButton";

    /// <summary>The name of the common visual states group.</summary>
    public const string CommonVisualStates = "CommonStates";

    /// <summary>The name of the normal visual state.</summary>
    public const string NormalVisualState = "Normal";

    /// <summary>The name of the pointer over visual state.</summary>
    public const string PointerOverVisualState = "PointerOver";

    /// <summary>The name of the selected visual state.</summary>
    public const string SelectedVisualState = "Selected";

    /// <summary>The name of the selected pointer over visual state.</summary>
    public const string SelectedPointerOverVisualState = "SelectedPointerOver";

    /// <summary>
    ///     Minimum space that must remain for dragging the tab, even when the tool buttons are overlayed.
    /// </summary>
    internal const int MinDragWidth = 40;

    private Grid? rootGrid;
    private ContentPresenter? iconPart;
    private TextBlock? headerPart;
    private StackPanel? buttonsContainer;
    private Button? pinButton;
    private Button? closeButton;
    private ILogger? logger;
    private bool isPointerOver;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStripItem"/> class.
    /// </summary>
    public TabStripItem()
    {
        this.DefaultStyleKey = typeof(TabStripItem);
    }

    /// <summary>
    /// Applies the control template and sets up template parts.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.rootGrid = this.GetTemplateChild(RootGridPartName) as Grid;
        this.iconPart = this.GetTemplateChild(IconPartName) as ContentPresenter;
        this.headerPart = this.GetTemplateChild(HeaderPartName) as TextBlock;
        this.buttonsContainer = this.GetTemplateChild(ButtonsContainerPartName) as StackPanel;
        this.pinButton = this.GetTemplateChild(PinButtonPartName) as Button;
        this.closeButton = this.GetTemplateChild(CloseButtonPartName) as Button;

        if (this.pinButton is not null)
        {
            this.pinButton.Click += this.PinButton_Click;
        }

        if (this.closeButton is not null)
        {
            this.closeButton.Click += this.CloseButton_Click;
        }

        if (this.rootGrid is not null)
        {
            this.rootGrid.PointerEntered -= this.OnRootGridPointerEntered;
            this.rootGrid.PointerExited -= this.OnRootGridPointerExited;
            this.rootGrid.PointerEntered += this.OnRootGridPointerEntered;
            this.rootGrid.PointerExited += this.OnRootGridPointerExited;
        }

        this.UpdateLayoutForCompactMode();
        this.UpdateMinWidth();
        this.UpdatePinGlyph();
        this.UpdateVisualState(useTransitions: false);
    }

    private void OnRootGridPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        this.isPointerOver = true;
        this.UpdateVisualState(useTransitions: true);

        // Show buttons in compact mode on hover
        if (this.IsCompact && this.buttonsContainer is not null)
        {
            this.buttonsContainer.Visibility = Visibility.Visible;
        }
    }

    private void OnRootGridPointerExited(object sender, PointerRoutedEventArgs e)
    {
        this.isPointerOver = false;
        this.UpdateVisualState(useTransitions: true);

        // Hide buttons in compact mode on exit
        if (this.IsCompact && this.buttonsContainer is not null)
        {
            this.buttonsContainer.Visibility = Visibility.Collapsed;
        }
    }

    private void OnItemChanged(DependencyPropertyChangedEventArgs e)
    {
        this.DataContext = e.NewValue;
        this.Content = e.NewValue; // For ContentPresenter if needed
        if (e.OldValue is TabItem oldItem)
        {
            oldItem.PropertyChanged -= this.TabItem_OnPropertyChanged;
        }

        if (e.NewValue is TabItem newItem)
        {
            newItem.PropertyChanged += this.TabItem_OnPropertyChanged;
        }

        this.UpdatePinGlyph();
        this.UpdateVisualState(useTransitions: false);
    }

    private void TabItem_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(TabItem.IsSelected), System.StringComparison.Ordinal))
        {
            this.UpdateVisualState(useTransitions: true);
        }
        else if (string.Equals(e.PropertyName, nameof(TabItem.IsPinned), System.StringComparison.Ordinal))
        {
            this.UpdatePinGlyph();
        }
    }

    private void UpdateVisualState(bool useTransitions)
    {
        string state;
        if (this.Item is { IsSelected: true })
        {
            state = this.isPointerOver ? SelectedPointerOverVisualState : SelectedVisualState;
        }
        else
        {
            state = this.isPointerOver ? PointerOverVisualState : NormalVisualState;
        }

        _ = VisualStateManager.GoToState(this, state, useTransitions);
    }

    private void PinButton_Click(object? sender, RoutedEventArgs e)
    {
        this.Item?.IsPinned = !this.Item.IsPinned;
    }

    private void CloseButton_Click(object? sender, RoutedEventArgs e)
    {
        if (this.Item is not null)
        {
            this.CloseRequested?.Invoke(this, new TabCloseRequestedEventArgs { Item = this.Item });
        }
    }

    private void UpdatePinGlyph()
    {
        if (this.pinButton?.Content is FontIcon fi)
        {
            fi.Glyph = this.Item?.IsPinned == true ? "\uE77A" : "\uE718";
        }
    }

    private void UpdateMinWidth()
    {
        var marginLeft = this.buttonsContainer?.Margin.Left ?? 0;
        var marginRight = this.buttonsContainer?.Margin.Right ?? 0;
        var pinWidth = this.pinButton?.Width ?? 0;
        var closeWidth = this.closeButton?.Width ?? 0;
        var spacing = (pinWidth > 0 && closeWidth > 0) ? this.buttonsContainer?.Spacing ?? 0 : 0;
        this.MinWidth = MinDragWidth + marginLeft + pinWidth + closeWidth + spacing + marginRight;
    }

    private void UpdateLayoutForCompactMode()
    {
        if (this.buttonsContainer is not null)
        {
            Grid.SetColumn(this.buttonsContainer, 2);
            this.buttonsContainer.HorizontalAlignment = HorizontalAlignment.Right;
            this.buttonsContainer.Visibility = this.IsCompact ? Visibility.Collapsed : Visibility.Visible;
        }
    }
}
