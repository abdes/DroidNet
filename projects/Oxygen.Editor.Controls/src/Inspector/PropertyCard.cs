// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.UI.ViewManagement;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls;

/// <summary>
///     A styled card control for displaying a property and its value in a property editor UI.
///     Supports visual states for normal, pointer-over, and disabled interactions.
/// </summary>
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

    private readonly UISettings uiSettings = new();
    private Grid? layoutRoot;
    private FrameworkElement? header;
    private ContentPresenter? editor;
    private TextBlock? headerQualifier;
    private TextBlock? suffix;
    private ContentPresenter? leading;
    private PropertyLayout? appliedLayout;

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyCard"/> class.
    /// </summary>
    public PropertyCard()
    {
        this.DefaultStyleKey = typeof(PropertyCard);
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
    ///     Loaded rows with the same name in the same window reserve the widest qualifier or unit among them after their
    ///     editor, so rows with different annotations, or none, line up their input fields.
    /// </remarks>
    public string QualifierGroup { get => (string)this.GetValue(QualifierGroupProperty); set => this.SetValue(QualifierGroupProperty, value); }

    /// <summary>Gets or sets the accessory preceding the editor in its value row.</summary>
    public object? LeadingContent { get => this.GetValue(LeadingContentProperty); set => this.SetValue(LeadingContentProperty, value); }

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
        this.appliedLayout = null;
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
    ///     The row goes inline when the label column and the editor's natural width both fit. The editor's natural
    ///     width is its desired width without a limit, so editors that rearrange themselves when narrow, such as
    ///     vectors, report the width at which they would have to.
    /// </remarks>
    protected override Size MeasureOverride(Size availableSize)
    {
        if (this.layoutRoot is not null && this.header is not null && this.editor is not null
            && this.headerQualifier is not null && this.suffix is not null)
        {
            var scale = Math.Max(this.uiSettings.TextScaleFactor, this.FontSize / 14);
            this.editor.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var qualifierWidth = this.MeasureQualifier();
            if (this.LeadingContent is not null)
            {
                this.leading?.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            }

            var accessoryWidth = this.LeadingContent is null ? 0 : (this.leading?.DesiredSize.Width ?? 0) + 4;
            var trailingWidth = qualifierWidth + ((this.Content as NumberBox)?.UnitWidth ?? 0);
            var reserve = this.AlignWithQualifierGroup(trailingWidth) - trailingWidth;
            var minimum = this.editor.DesiredSize.Width + qualifierWidth + accessoryWidth + reserve;
            var width = availableSize.Width - this.Padding.Left - this.Padding.Right;
            var layout = this.Layout == PropertyLayout.Auto
                ? (width >= 12 + Math.Max(this.LabelWidth * scale / InlineLabelWidthRatio, minimum / (1 - InlineLabelWidthRatio))
                    ? PropertyLayout.Inline : PropertyLayout.Stacked)
                : this.Layout;
            this.ActualLayout = layout;
            this.header.Visibility = this.IsHeaderVisible ? Visibility.Visible : Visibility.Collapsed;
            var headerAnnotation = this.IsCompound && layout == PropertyLayout.Stacked && this.IsHeaderVisible;
            this.headerQualifier.Visibility = headerAnnotation && !string.IsNullOrEmpty(this.Qualifier) ? Visibility.Visible : Visibility.Collapsed;
            this.suffix.Visibility = !headerAnnotation && !string.IsNullOrEmpty(this.Qualifier) ? Visibility.Visible : Visibility.Collapsed;
            this.leading?.Visibility = this.LeadingContent is null ? Visibility.Collapsed : Visibility.Visible;
            var valueGroup = (FrameworkElement)this.editor.Parent;

            // A stacked compound row moves its qualifier to the header and spans the row.
            var margin = new Thickness(0, 0, headerAnnotation ? 0 : reserve, 0);
            if (valueGroup.Margin != margin)
            {
                valueGroup.Margin = margin;
            }

            if (layout != this.appliedLayout)
            {
                this.layoutRoot.ColumnDefinitions.Clear();
                this.layoutRoot.RowDefinitions.Clear();
                this.layoutRoot.ColumnSpacing = layout == PropertyLayout.Inline ? 12 : 0;
                this.layoutRoot.RowSpacing = this.IsHeaderVisible && layout == PropertyLayout.Stacked ? 4 : 0;
                this.layoutRoot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(layout == PropertyLayout.Inline ? InlineLabelWidthRatio : 1, GridUnitType.Star) });
                if (layout == PropertyLayout.Inline)
                {
                    this.layoutRoot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1 - InlineLabelWidthRatio, GridUnitType.Star) });
                }

                this.layoutRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                this.layoutRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                Grid.SetRow(valueGroup, layout == PropertyLayout.Inline ? 0 : 1);
                Grid.SetColumn(valueGroup, layout == PropertyLayout.Inline ? 1 : 0);
                this.appliedLayout = layout;
            }
        }

        return base.MeasureOverride(availableSize);
    }

    private static DependencyProperty RegisterLayoutProperty(string name, Type type, object? defaultValue)
        => DependencyProperty.Register(name, type, typeof(PropertyCard), new PropertyMetadata(defaultValue, (d, _) =>
        {
            var card = (PropertyCard)d;
            card.appliedLayout = null;
            card.InvalidateMeasure();
        }));

    // The qualifier's width after the editor, including its gap; zero without one.
    private double MeasureQualifier()
    {
        if (string.IsNullOrEmpty(this.Qualifier))
        {
            return 0;
        }

        var annotation = new TextBlock { Text = this.Qualifier, FontSize = 12, FontFamily = this.FontFamily };
        annotation.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return annotation.DesiredSize.Width + 4;
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
