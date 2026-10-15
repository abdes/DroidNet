// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace Oxygen.Editor.Controls;

/// <summary>
/// Represents a custom expander control for property editing in the Oxygen World Editor.
/// </summary>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = ActionIconPresenterHolderPartName, Type = typeof(Viewbox))]
[TemplatePart(Name = HeaderPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = DescriptionPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = HeaderIconPresenterHolderPartName, Type = typeof(Viewbox))]
[TemplatePart(Name = ItemsRepeaterPartName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = ContentPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = HeaderPanelPartName, Type = typeof(FrameworkElement))]

[TemplateVisualState(Name = NormalState, GroupName = CommonStates)]
[TemplateVisualState(Name = PointerOverState, GroupName = CommonStates)]
[TemplateVisualState(Name = DisabledState, GroupName = CommonStates)]
public partial class PropertiesExpander : Control
{
    private const string CommonStates = "CommonStates";
    private const string NormalState = "Normal";
    private const string PointerOverState = "PointerOver";
    private const string DisabledState = "Disabled";

    private const string ActionIconPresenterHolderPartName = "PartActionIconPresenterHolder";
    private const string RootGridPartName = "PartRootGrid";
    private const string HeaderPresenterPartName = "PartHeaderPresenter";
    private const string DescriptionPresenterPartName = "PartDescriptionPresenter";
    private const string HeaderIconPresenterHolderPartName = "PartHeaderIconPresenterHolder";
    private const string ItemsRepeaterPartName = "PartItemsRepeater";
    private const string ContentPresenterPartName = "PartContentPresenter";
    private const string HeaderPanelPartName = "HeaderPanel";

    // The gap between the title and content shown beside it.
    private const double HeaderContentGap = 12;

    private ContentPresenter? headerContentPresenter;
    private FrameworkElement? headerPanel;
    private FrameworkElement? headerIconHolder;

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertiesExpander"/> class.
    /// </summary>
    public PropertiesExpander()
    {
        this.DefaultStyleKey = typeof(PropertiesExpander);

        this.Items = [];
        _ = this.RegisterPropertyChangedCallback(HorizontalContentAlignmentProperty, static (d, _) => ((PropertiesExpander)d).InvalidateMeasure());
    }

    /// <summary>Expands and realizes a property item before bringing it into its scrolling host's view.</summary>
    /// <param name="item">An item owned by this expander.</param>
    /// <returns>Whether the item was found and realized.</returns>
    public bool BringItemIntoView(object item)
    {
        var index = this.Items.IndexOf(item);
        if (index < 0)
        {
            return false;
        }

        this.IsExpanded = true;
        _ = this.ApplyTemplate();
        if (this.GetTemplateChild(ItemsRepeaterPartName) is not ItemsRepeater repeater)
        {
            return false;
        }

        var element = repeater.GetOrCreateElement(index);
        repeater.UpdateLayout();
        element.StartBringIntoView();
        return true;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.IsEnabledChanged -= this.OnIsEnabledChanged;

        this.headerContentPresenter = this.GetTemplateChild(ContentPresenterPartName) as ContentPresenter;
        this.headerPanel = this.GetTemplateChild(HeaderPanelPartName) as FrameworkElement;
        this.headerIconHolder = this.GetTemplateChild(HeaderIconPresenterHolderPartName) as FrameworkElement;
        this.OnHeaderChanged();
        this.OnDescriptionChanged();

        this.CheckInitialVisualState();

        this.PointerEntered += this.ControlPointerEntered;
        this.PointerExited += this.ControlPointerExited;

        this.IsEnabledChanged += this.OnIsEnabledChanged;
    }

    /// <inheritdoc />
    /// <remarks>
    ///     The header lays itself out for the width its parent offers: narrower than <see cref="HeaderWrapThreshold"/>,
    ///     the header content moves under the title; narrower than <see cref="HeaderIconThreshold"/>, the icon hides too.
    ///     The choice depends on the offered width, never on the width the expander ends up with, so it cannot change the
    ///     size it is made from, whether the parent stretches the expander or sizes it to its content.
    /// </remarks>
    protected override Size MeasureOverride(Size availableSize)
    {
        this.LayOutHeader(availableSize.Width);
        return base.MeasureOverride(availableSize);
    }

    private static bool IsNullOrEmptyString(object? obj)
        => obj == null || (obj is string objString && string.IsNullOrEmpty(objString));

    private void CheckInitialVisualState()
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    private void ControlPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        this.OnPointerEntered(e);
        _ = VisualStateManager.GoToState(this, PointerOverState, useTransitions: true);
    }

    private void ControlPointerExited(object sender, PointerRoutedEventArgs e)
    {
        this.OnPointerExited(e);
        _ = VisualStateManager.GoToState(this, NormalState, useTransitions: true);
    }

    private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    // Places header content beside the title, or under it when narrow, and shows the icon unless very narrow.
    private void LayOutHeader(double offeredWidth)
    {
        var hasContent = !IsNullOrEmptyString(this.Content);
        var wrapped = offeredWidth < this.HeaderWrapThreshold;
        if (this.headerContentPresenter is { } presenter)
        {
            presenter.Visibility = hasContent ? Visibility.Visible : Visibility.Collapsed;
            Grid.SetRow(presenter, wrapped ? 1 : 0);
            Grid.SetColumn(presenter, wrapped ? 1 : 2);
            presenter.HorizontalAlignment = wrapped ? HorizontalAlignment.Stretch : HorizontalAlignment.Right;
            presenter.HorizontalContentAlignment = wrapped ? HorizontalAlignment.Left : this.HorizontalContentAlignment;
        }

        // The gap separates the title from content beside it; without such content the title takes the space.
        var titleMargin = new Thickness(0, 0, hasContent && !wrapped ? HeaderContentGap : 0, 0);
        if (this.headerPanel is { } panel && panel.Margin != titleMargin)
        {
            panel.Margin = titleMargin;
        }

        this.headerIconHolder?.Visibility = this.HeaderIcon is not null && offeredWidth >= this.HeaderIconThreshold ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnDescriptionChanged()
    {
        if (this.GetTemplateChild(DescriptionPresenterPartName) is FrameworkElement descriptionPresenter)
        {
            descriptionPresenter.Visibility = IsNullOrEmptyString(this.Description) ? Visibility.Collapsed : Visibility.Visible;
        }
    }

    private void OnHeaderChanged()
    {
        if (this.GetTemplateChild(HeaderPresenterPartName) is FrameworkElement headerPresenter)
        {
            headerPresenter.Visibility = IsNullOrEmptyString(this.Header) ? Visibility.Collapsed : Visibility.Visible;
        }
    }
}
