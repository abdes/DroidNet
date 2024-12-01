// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace DroidNet.Docking.Controls;

/// <summary>
/// Represents a tab within a dock panel dockable tabs control that can display an icon and a title
/// for the corresponding <see cref="Dockable"/> view.
/// </summary>
/// <remarks>
/// The <see cref="DockableTab"/> control is designed to be used within a docking framework. It
/// supports displaying an icon and a title, and it can respond to pointer events (pointer pressed
/// and tapped) to indicate selection and activation.
/// <para>
/// If the <see cref="IconConverter"/> property is not set, a default icon will be used.
/// </para>
/// <para>
/// To support the precise measurement of the tab width, the control requires a style with the key
/// "TabIconStyle" specifying the tab icon style, and a style with the key "TabTitleStyle"
/// specifying the tab title style. Both styles should include setters that may impact the width
/// measureents of the elements.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code><![CDATA[
/// <local:DockableTab
///     Dockable="{Binding SomeDockable}"
///     IconConverter="{StaticResource SomeIconConverter}"
///     IsTitleVisible="True"
///     TextTrimming="CharacterEllipsis"
///     IsSelected="False"
///     TabActivated="OnTabActivated" />
/// ]]></code>
/// </example>
[TemplatePart(Name = PartRootGridName, Type = typeof(Grid))]
[TemplatePart(Name = PartTabIconName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = PartTabTitleName, Type = typeof(TextBlock))]
[TemplatePart(Name = PartSelectionBarName, Type = typeof(Border))]
[TemplateVisualState(Name = StateNormal, GroupName = GroupPointerStates)]
[TemplateVisualState(Name = StatePointerOver, GroupName = GroupPointerStates)]
[TemplateVisualState(Name = StateUnselected, GroupName = GroupSelectionStates)]
[TemplateVisualState(Name = StateSelected, GroupName = GroupSelectionStates)]
public sealed partial class DockableTab : Control
{
    /// <summary>
    /// The name of the root grid part in the control template.
    /// </summary>
    public const string PartRootGridName = "PartRootGrid";

    /// <summary>
    /// The name of the tab icon part in the control template.
    /// </summary>
    public const string PartTabIconName = "PartTabIcon";

    /// <summary>
    /// The name of the tab title part in the control template.
    /// </summary>
    public const string PartTabTitleName = "PartTabTitle";

    /// <summary>
    /// The name of the selection bar part in the control template.
    /// </summary>
    public const string PartSelectionBarName = "PartSelectionBar";

    /// <summary>
    /// The name of the normal visual state.
    /// </summary>
    public const string StateNormal = "Normal";

    /// <summary>
    /// The name of the pointer over visual state.
    /// </summary>
    public const string StatePointerOver = "PointerOver";

    /// <summary>
    /// The name of the unselected visual state.
    /// </summary>
    public const string StateUnselected = "Unselected";

    /// <summary>
    /// The name of the selected visual state.
    /// </summary>
    public const string StateSelected = "Selected";

    /// <summary>
    /// The name of the pointer states visual state group.
    /// </summary>
    public const string GroupPointerStates = "PointerStates";

    /// <summary>
    /// The name of the selection states visual state group.
    /// </summary>
    public const string GroupSelectionStates = "SelectionStates";

    private const string TabIconStyleKey = "TabIconStyle";
    private const string TabTitleStyleKey = "TabTitleStyle";

    private ContentPresenter? iconContentPresenter;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockableTab"/> class.
    /// </summary>
    public DockableTab()
    {
        this.DefaultStyleKey = typeof(DockableTab);

        // Add the resource dictionary to the control's resources
        var assemblyName = Assembly.GetExecutingAssembly().GetName().Name;
        var resourceDictionary = new ResourceDictionary
        {
            Source = new Uri($"ms-appx:///{assemblyName}/Controls/DockableTab.xaml"),
        };
        this.Resources.MergedDictionaries.Add(resourceDictionary);
        Debug.Assert(this.Resources.ContainsKey(TabIconStyleKey), $"a style with Key=`{TabIconStyleKey}` is needed for measurement");
        Debug.Assert(this.Resources.ContainsKey(TabTitleStyleKey), $"a style with Key=`{TabTitleStyleKey}` is needed for measurement");

        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.PointerPressed += this.OnPointerPressed;
        this.Tapped += this.OnTapped;
    }

    /// <summary>
    /// Occurs when the tab is activated.
    /// </summary>
    public event EventHandler<TabActivatedEventArgs>? TabActivated;

    /// <summary>
    /// Measures the desired width of the tab, including the icon and title.
    /// </summary>
    /// <returns>The desired width of the tab.</returns>
    public double MeasureDesiredWidth() => this.MeasureIcon() + this.MeasureTitle();

    /// <summary>
    /// Applies the control template and initializes template parts.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        this.iconContentPresenter = this.GetTemplateChild(PartTabIconName) as ContentPresenter ?? throw new InvalidOperationException($"{nameof(DockableTab)} template is missing {PartTabIconName}");

        this.UpdateIcon();
        this.UpdateSelectionState();

        base.OnApplyTemplate();
    }

    private double MeasureIcon()
    {
        // Create a ContentPresenter and apply the TabIconStyle if it exists
        var contentPresenter = new ContentPresenter
        {
            Style = (Style)this.Resources[TabIconStyleKey],
        };

        // Measure the width required for the icon
        contentPresenter.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var iconWidth = contentPresenter.DesiredSize.Width;
        iconWidth += contentPresenter.Margin.Left + contentPresenter.Margin.Right;
        return iconWidth;
    }

    private double MeasureTitle()
    {
        // Measure the width required to display the text content without trimming
        var textBlock = new TextBlock
        {
            Style = (Style)this.Resources[TabTitleStyleKey],
            Text = this.Dockable.TabbedTitle,
            TextTrimming = TextTrimming.None,
        };

        textBlock.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var textWidth = textBlock.DesiredSize.Width;

        // Include the margin in the measurement
        textWidth += textBlock.Margin.Left + textBlock.Margin.Right;
        return textWidth;
    }

    private void UpdateIcon()
    {
        if (this.Dockable == null || this.iconContentPresenter is null)
        {
            return;
        }

        this.iconContentPresenter.Content = (this.IconConverter != null)
            ? this.IconConverter.Convert(this.Dockable, typeof(object), parameter: null, language: null)
            : new FontIcon { Glyph = "\uF592", FontSize = 12 };
    }

    private void UpdateSelectionState() => _ = this.IsSelected
            ? this.DispatcherQueue.TryEnqueue(() => VisualStateManager.GoToState(this, "Selected", true))
            : this.DispatcherQueue.TryEnqueue(() => VisualStateManager.GoToState(this, "Unselected", true));

    private void OnPointerEntered(object sender, PointerRoutedEventArgs args)
        => this.DispatcherQueue.TryEnqueue(() => VisualStateManager.GoToState(this, "PointerOver", useTransitions: true));

    private void OnPointerExited(object sender, PointerRoutedEventArgs args)
        => this.DispatcherQueue.TryEnqueue(() => VisualStateManager.GoToState(this, "Normal", useTransitions: true));

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        _ = sender; // Unused
        _ = args; // Unused

        if (!this.IsSelected)
        {
            this.TabActivated?.Invoke(this, new TabActivatedEventArgs());
        }
    }

    private void OnTapped(object sender, TappedRoutedEventArgs args)
    {
        _ = sender; // Unused
        _ = args; // Unused

        if (!this.IsSelected)
        {
            this.TabActivated?.Invoke(this, new TabActivatedEventArgs());
        }
    }
}
