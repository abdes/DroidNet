// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using IPreferredWidthSource = DroidNet.Controls.IPreferredWidthSource;
using NumberBox = DroidNet.Controls.NumberBox;
using TextMeasurer = DroidNet.Controls.TextMeasurer;
using TextScale = DroidNet.Controls.TextScale;

namespace Oxygen.Editor.Controls;

/// <summary>
///     A styled card control for displaying a property and its value in a property editor UI.
///     Supports visual states for normal, pointer-over, and disabled interactions.
/// </summary>
/// <remarks>
///     A row whose value overrides its source, such as a surface's asset default, shows a bar in the gutter before it:
///     full strength when every edited object overrides it, half strength when only some do. While it is marked and has
///     a <see cref="ResetCommand"/>, a trailing button resets it to the source.
/// </remarks>
[TemplateVisualState(Name = NormalState, GroupName = CommonStates)]
[TemplateVisualState(Name = PointerOverState, GroupName = CommonStates)]
[TemplateVisualState(Name = DisabledState, GroupName = CommonStates)]
public partial class PropertyCard : ContentControl
{
    /// <summary>The name of the visual state group for common states.</summary>
    public const string CommonStates = "CommonStates";

    /// <summary>The visual state name for the normal state.</summary>
    public const string NormalState = "Normal";

    /// <summary>The visual state name for the pointer-over state.</summary>
    public const string PointerOverState = "PointerOver";

    /// <summary>The visual state name for the disabled state.</summary>
    public const string DisabledState = "Disabled";

    /// <summary>Identifies the automatic, inline or stacked layout policy.</summary>
    public static readonly DependencyProperty LayoutProperty = RegisterLayoutProperty(nameof(Layout), typeof(PropertyLayout), PropertyLayout.Auto);

    /// <summary>Identifies the minimum label-column width used for automatic stacking.</summary>
    public static readonly DependencyProperty LabelWidthProperty = RegisterLayoutProperty(nameof(LabelWidth), typeof(double), 124d);

    /// <summary>Identifies the shared value annotation, such as metres or linear RGB.</summary>
    public static readonly DependencyProperty QualifierProperty = RegisterLayoutProperty(nameof(Qualifier), typeof(string), string.Empty);

    /// <summary>Identifies compound editors whose stacked annotation belongs in the header.</summary>
    public static readonly DependencyProperty IsCompoundProperty = RegisterLayoutProperty(nameof(IsCompound), typeof(bool), defaultValue: false);

    /// <summary>Identifies whether a composition supplies its own navigation instead of a property header.</summary>
    public static readonly DependencyProperty IsHeaderVisibleProperty = RegisterLayoutProperty(nameof(IsHeaderVisible), typeof(bool), defaultValue: true);

    /// <summary>Identifies the name of the rows whose editors end at the same right edge.</summary>
    public static readonly DependencyProperty QualifierGroupProperty = DependencyProperty.Register(
        nameof(QualifierGroup), typeof(string), typeof(PropertyCard), new PropertyMetadata(string.Empty, OnQualifierGroupChanged));

    /// <summary>Identifies a value-group accessory, such as a color swatch.</summary>
    public static readonly DependencyProperty LeadingContentProperty = RegisterLayoutProperty(nameof(LeadingContent), typeof(object), defaultValue: null);

    /// <summary>Identifies how many edited objects override the value.</summary>
    public static readonly DependencyProperty OverrideStateProperty = RegisterOverrideProperty(nameof(OverrideState), typeof(OverrideState), OverrideState.None);

    /// <summary>Identifies the command that resets the value to its source.</summary>
    public static readonly DependencyProperty ResetCommandProperty = RegisterOverrideProperty(nameof(ResetCommand), typeof(ICommand), defaultValue: null);

    /// <summary>Identifies the reset button's tooltip and automation name.</summary>
    public static readonly DependencyProperty ResetToolTipProperty = DependencyProperty.Register(
        nameof(ResetToolTip), typeof(string), typeof(PropertyCard), new PropertyMetadata("Reset to the source value"));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="PropertyName"/> property.
    /// </summary>
    public static readonly DependencyProperty PropertyNameProperty =
        DependencyProperty.Register(
            nameof(PropertyName),
            typeof(string),
            typeof(PropertyCard),
            new PropertyMetadata(default(string)));

    private const double InlineLabelWidthRatio = 0.4;

    private Grid? layoutRoot;
    private FrameworkElement? header;
    private ContentPresenter? editor;
    private TextBlock? headerQualifier;
    private TextBlock? suffix;
    private ContentPresenter? leading;
    private FrameworkElement? overrideBar;
    private Button? resetButton;
    private PropertyLayout? appliedLayout;

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyCard"/> class.
    /// </summary>
    public PropertyCard()
    {
        this.DefaultStyleKey = typeof(PropertyCard);
        TextScale.InvalidateMeasureOnChange(this);
        this.Loaded += (_, _) => this.InvalidateMeasure();
        this.Unloaded += (_, _) => this.LeaveQualifierGroup();
        _ = this.RegisterPropertyChangedCallback(VisibilityProperty, static (d, _) => ((PropertyCard)d).OnVisibilityChanged());
    }

    /// <summary>Gets or sets the row layout policy.</summary>
    public PropertyLayout Layout { get => (PropertyLayout)this.GetValue(LayoutProperty); set => this.SetValue(LayoutProperty, value); }

    /// <summary>Gets or sets the minimum label width in DIPs used for automatic stacking.</summary>
    public double LabelWidth { get => (double)this.GetValue(LabelWidthProperty); set => this.SetValue(LabelWidthProperty, value); }

    /// <summary>Gets or sets the read-only qualifier of the whole value, such as metres for a position.</summary>
    /// <remarks>A scalar's unit belongs to its NumberBox, inside the editor.</remarks>
    public string Qualifier { get => (string)this.GetValue(QualifierProperty); set => this.SetValue(QualifierProperty, value); }

    /// <summary>Gets or sets a value indicating whether the value is a compound editor.</summary>
    public bool IsCompound { get => (bool)this.GetValue(IsCompoundProperty); set => this.SetValue(IsCompoundProperty, value); }

    /// <summary>Gets or sets a value indicating whether to display the property header.</summary>
    public bool IsHeaderVisible { get => (bool)this.GetValue(IsHeaderVisibleProperty); set => this.SetValue(IsHeaderVisibleProperty, value); }

    /// <summary>Gets or sets the name of the rows whose editors end at the same right edge; empty for none.</summary>
    /// <remarks>
    ///     Loaded, visible rows with the same name in the same section, the <see cref="PropertiesExpander"/> that shows
    ///     them or the root of their visual tree outside one, reserve the widest qualifier or unit among them after
    ///     their editor, so rows with different annotations, or none, line up their input fields.
    /// </remarks>
    public string QualifierGroup { get => (string)this.GetValue(QualifierGroupProperty); set => this.SetValue(QualifierGroupProperty, value); }

    /// <summary>Gets or sets the accessory preceding the editor in its value row.</summary>
    public object? LeadingContent { get => this.GetValue(LeadingContentProperty); set => this.SetValue(LeadingContentProperty, value); }

    /// <summary>Gets or sets how many edited objects override the value; the bar shows unless it is none.</summary>
    public OverrideState OverrideState { get => (OverrideState)this.GetValue(OverrideStateProperty); set => this.SetValue(OverrideStateProperty, value); }

    /// <summary>Gets or sets the command that resets the value to its source; the reset button shows while the value is overridden.</summary>
    public ICommand? ResetCommand { get => (ICommand?)this.GetValue(ResetCommandProperty); set => this.SetValue(ResetCommandProperty, value); }

    /// <summary>Gets or sets the reset button's tooltip, which names the source, such as "Reset to the asset value (On)".</summary>
    public string ResetToolTip { get => (string)this.GetValue(ResetToolTipProperty); set => this.SetValue(ResetToolTipProperty, value); }

    /// <summary>Gets the layout selected during measurement.</summary>
    public PropertyLayout ActualLayout { get; private set; }

    /// <summary>
    /// Gets or sets the name of the property displayed by this card.
    /// </summary>
    public string PropertyName
    {
        get => (string)this.GetValue(PropertyNameProperty);
        set => this.SetValue(PropertyNameProperty, value);
    }

    /// <summary>
    /// Applies the control template and wires up visual state event handlers.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.layoutRoot = this.GetTemplateChild("RootGrid") as Grid;
        this.header = this.GetTemplateChild("PropertyHeader") as FrameworkElement;
        this.editor = this.GetTemplateChild("PropertyEditor") as ContentPresenter;
        this.headerQualifier = this.GetTemplateChild("HeaderQualifier") as TextBlock;
        this.suffix = this.GetTemplateChild("ValueQualifier") as TextBlock;
        this.leading = this.GetTemplateChild("ValueLeading") as ContentPresenter;
        this.overrideBar = this.GetTemplateChild("OverrideBar") as FrameworkElement;
        this.resetButton = this.GetTemplateChild("ResetButton") as Button;
        this.appliedLayout = null;
        this.UpdateOverrideVisuals();
        this.IsEnabledChanged -= this.OnIsEnabledChanged;
        this.PointerEntered -= this.OnPointerEntered;
        this.PointerExited -= this.OnPointerExited;
        this.CheckInitialVisualState();

        this.PointerEntered += this.OnPointerEntered;
        this.PointerExited += this.OnPointerExited;
        this.IsEnabledChanged += this.OnIsEnabledChanged;
    }

    /// <inheritdoc />
    /// <remarks>
    ///     The row goes inline when the label column and the editor's preferred width both fit. An editor that
    ///     rearranges itself when narrow, such as a number, vector or color editor, reports that width through
    ///     <see cref="IPreferredWidthSource"/> without being measured, so the probe never rearranges it for a width it
    ///     does not get. Other content keeps a fixed natural size and is measured without a width limit.
    /// </remarks>
    protected override Size MeasureOverride(Size availableSize)
    {
        if (this.layoutRoot is not null && this.header is not null && this.editor is not null
            && this.headerQualifier is not null && this.suffix is not null)
        {
            var scale = Math.Max(TextScale.Factor, this.FontSize / 14);
            var editorWidth = this.EditorPreferredWidth();
            var qualifierWidth = string.IsNullOrEmpty(this.Qualifier) ? 0 : TextMeasurer.Width(this.Qualifier, this.suffix) + this.suffix.Margin.Left;
            if (this.LeadingContent is not null)
            {
                this.leading?.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            }

            var accessoryWidth = this.LeadingContent is null ? 0 : (this.leading?.DesiredSize.Width ?? 0) + 4;
            var trailingWidth = qualifierWidth + ((this.Content as NumberBox)?.UnitWidth ?? 0);
            var reserve = this.AlignWithQualifierGroup(trailingWidth) - trailingWidth;
            var resetWidth = 0d;
            if (this.resetButton is { Visibility: Visibility.Visible } reset)
            {
                reset.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
                resetWidth = reset.DesiredSize.Width;
            }

            // The reset button ends the value, so it narrows the editor, never the label column.
            var minimum = editorWidth + qualifierWidth + accessoryWidth + reserve + resetWidth;
            var rowWidth = availableSize.Width - this.Padding.Left - this.Padding.Right;
            var chosenLayout = this.Layout == PropertyLayout.Auto
                ? (rowWidth >= 12 + Math.Max(this.LabelWidth * scale / InlineLabelWidthRatio, minimum / (1 - InlineLabelWidthRatio))
                    ? PropertyLayout.Inline : PropertyLayout.Stacked)
                : this.Layout;
            this.ActualLayout = chosenLayout;
            this.header.Visibility = this.IsHeaderVisible ? Visibility.Visible : Visibility.Collapsed;
            var headerAnnotation = this.IsCompound && chosenLayout == PropertyLayout.Stacked && this.IsHeaderVisible;
            this.headerQualifier.Visibility = headerAnnotation && !string.IsNullOrEmpty(this.Qualifier) ? Visibility.Visible : Visibility.Collapsed;
            this.suffix.Visibility = !headerAnnotation && !string.IsNullOrEmpty(this.Qualifier) ? Visibility.Visible : Visibility.Collapsed;
            this.leading?.Visibility = this.LeadingContent is null ? Visibility.Collapsed : Visibility.Visible;
            var valueGroup = (FrameworkElement)this.editor.Parent;

            // A stacked compound row moves its qualifier to the header and spans the row.
            var valueGroupMargin = new Thickness(0, 0, headerAnnotation ? 0 : reserve, 0);
            if (valueGroup.Margin != valueGroupMargin)
            {
                valueGroup.Margin = valueGroupMargin;
            }

            if (chosenLayout != this.appliedLayout)
            {
                this.layoutRoot.ColumnDefinitions.Clear();
                this.layoutRoot.RowDefinitions.Clear();
                this.layoutRoot.ColumnSpacing = chosenLayout == PropertyLayout.Inline ? 12 : 0;
                this.layoutRoot.RowSpacing = this.IsHeaderVisible && chosenLayout == PropertyLayout.Stacked ? 4 : 0;
                this.layoutRoot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(chosenLayout == PropertyLayout.Inline ? InlineLabelWidthRatio : 1, GridUnitType.Star) });
                if (chosenLayout == PropertyLayout.Inline)
                {
                    this.layoutRoot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1 - InlineLabelWidthRatio, GridUnitType.Star) });
                }

                this.layoutRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                this.layoutRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                Grid.SetRow(valueGroup, chosenLayout == PropertyLayout.Inline ? 0 : 1);
                Grid.SetColumn(valueGroup, chosenLayout == PropertyLayout.Inline ? 1 : 0);
                this.appliedLayout = chosenLayout;
            }
        }

        return base.MeasureOverride(availableSize);
    }

    private static DependencyProperty RegisterLayoutProperty(string propertyName, Type type, object? defaultValue)
        => DependencyProperty.Register(propertyName, type, typeof(PropertyCard), new PropertyMetadata(defaultValue, (d, _) =>
        {
            var card = (PropertyCard)d;
            card.appliedLayout = null;
            card.InvalidateMeasure();
        }));

    private static DependencyProperty RegisterOverrideProperty(string propertyName, Type type, object? defaultValue)
        => DependencyProperty.Register(propertyName, type, typeof(PropertyCard), new PropertyMetadata(defaultValue, (d, _) =>
        {
            var card = (PropertyCard)d;
            card.UpdateOverrideVisuals();
            card.InvalidateMeasure();
        }));

    private void UpdateOverrideVisuals()
    {
        this.overrideBar?.Opacity = OverrideStates.BarOpacity(this.OverrideState);
        this.overrideBar?.Visibility = this.OverrideState == OverrideState.None ? Visibility.Collapsed : Visibility.Visible;
        this.resetButton?.Visibility = this.ResetCommand is null ? Visibility.Collapsed : OverrideStates.ResetVisibility(this.OverrideState);
    }

    // The editor's one-row width with its margin: reported by a responsive editor, measured for fixed-size content.
    private double EditorPreferredWidth()
    {
        if (this.Content is IPreferredWidthSource source and FrameworkElement element)
        {
            return source.PreferredWidth + element.Margin.Left + element.Margin.Right;
        }

        this.editor!.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return this.editor.DesiredSize.Width;
    }

    /// <summary>
    /// Checks and sets the initial visual state based on the enabled state.
    /// </summary>
    private void CheckInitialVisualState()
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    /// <summary>
    /// Handles changes to the enabled state and updates the visual state accordingly.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    /// <summary>
    /// Handles pointer entering the control and updates the visual state to pointer-over.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnPointerEntered(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "PointerOver", useTransitions: true);

    /// <summary>
    /// Handles pointer exiting the control and updates the visual state to normal.
    /// </summary>
    /// <param name="sender">The sender object.</param>
    /// <param name="e">The event arguments.</param>
    private void OnPointerExited(object sender, PointerRoutedEventArgs e)
        => VisualStateManager.GoToState(this, "Normal", useTransitions: true);
}
