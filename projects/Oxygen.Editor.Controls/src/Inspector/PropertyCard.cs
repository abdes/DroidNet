// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.UI.ViewManagement;

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
    private const double InlineLabelWidthRatio = 0.4;

    private readonly UISettings uiSettings = new();
    private Grid? layoutRoot;
    private FrameworkElement? header;
    private ContentPresenter? editor;
    private TextBlock? headerQualifier;
    private TextBlock? suffix;
    private TextBlock? prefix;
    private ContentPresenter? leading;
    private PropertyLayout? appliedLayout;

    /// <summary>Identifies the automatic, inline or stacked layout policy.</summary>
    public static readonly DependencyProperty LayoutProperty = RegisterLayoutProperty(nameof(Layout), typeof(PropertyLayout), PropertyLayout.Auto);

    /// <summary>Identifies the minimum label-column width used for automatic stacking.</summary>
    public static readonly DependencyProperty LabelWidthProperty = RegisterLayoutProperty(nameof(LabelWidth), typeof(double), 124d);

    /// <summary>Identifies the minimum usable editor width, excluding annotations.</summary>
    public static readonly DependencyProperty EditorMinimumWidthProperty = RegisterLayoutProperty(nameof(EditorMinimumWidth), typeof(double), 128d);

    /// <summary>Identifies the shared value annotation, such as metres or linear RGB.</summary>
    public static readonly DependencyProperty QualifierProperty = RegisterLayoutProperty(nameof(Qualifier), typeof(string), string.Empty);

    /// <summary>Identifies the numeric prefix, such as f/ or 1/.</summary>
    public static readonly DependencyProperty PrefixProperty = RegisterLayoutProperty(nameof(Prefix), typeof(string), string.Empty);

    /// <summary>Identifies compound editors whose stacked annotation belongs in the header.</summary>
    public static readonly DependencyProperty IsCompoundProperty = RegisterLayoutProperty(nameof(IsCompound), typeof(bool), defaultValue: false);

    /// <summary>Identifies whether a composition supplies its own navigation instead of a property header.</summary>
    public static readonly DependencyProperty IsHeaderVisibleProperty = RegisterLayoutProperty(nameof(IsHeaderVisible), typeof(bool), defaultValue: true);

    /// <summary>Identifies the shared reserved scalar suffix slot.</summary>
    public static readonly DependencyProperty QualifierMinimumWidthProperty = RegisterLayoutProperty(nameof(QualifierMinimumWidth), typeof(double), 24d);

    /// <summary>Identifies a value-group accessory, such as a color swatch.</summary>
    public static readonly DependencyProperty LeadingContentProperty = RegisterLayoutProperty(nameof(LeadingContent), typeof(object), defaultValue: null);

    /// <summary>Identifies scalar compositions whose label and annotations are owned by their NumberBox.</summary>
    public static readonly DependencyProperty UseEditorLabelProperty = RegisterLayoutProperty(nameof(UseEditorLabel), typeof(bool), defaultValue: false);

    /// <summary>Gets or sets whether a single NumberBox supplies the interactive property label.</summary>
    public bool UseEditorLabel { get => (bool)this.GetValue(UseEditorLabelProperty); set => this.SetValue(UseEditorLabelProperty, value); }

    /// <summary>Gets or sets the row layout policy.</summary>
    public PropertyLayout Layout { get => (PropertyLayout)this.GetValue(LayoutProperty); set => this.SetValue(LayoutProperty, value); }

    /// <summary>Gets or sets the minimum label width in DIPs used for automatic stacking.</summary>
    public double LabelWidth { get => (double)this.GetValue(LabelWidthProperty); set => this.SetValue(LabelWidthProperty, value); }

    /// <summary>Gets or sets the minimum usable width of the editor in DIPs.</summary>
    public double EditorMinimumWidth { get => (double)this.GetValue(EditorMinimumWidthProperty); set => this.SetValue(EditorMinimumWidthProperty, value); }

    /// <summary>Gets or sets the read-only unit or value qualifier.</summary>
    public string Qualifier { get => (string)this.GetValue(QualifierProperty); set => this.SetValue(QualifierProperty, value); }

    /// <summary>Gets or sets the read-only numeric prefix.</summary>
    public string Prefix { get => (string)this.GetValue(PrefixProperty); set => this.SetValue(PrefixProperty, value); }

    /// <summary>Gets or sets whether the value is a compound editor.</summary>
    public bool IsCompound { get => (bool)this.GetValue(IsCompoundProperty); set => this.SetValue(IsCompoundProperty, value); }

    /// <summary>Gets or sets whether to display the property header.</summary>
    public bool IsHeaderVisible { get => (bool)this.GetValue(IsHeaderVisibleProperty); set => this.SetValue(IsHeaderVisibleProperty, value); }

    /// <summary>Gets or sets the reserved scalar suffix width.</summary>
    public double QualifierMinimumWidth { get => (double)this.GetValue(QualifierMinimumWidthProperty); set => this.SetValue(QualifierMinimumWidthProperty, value); }

    /// <summary>Gets or sets the accessory preceding the editor in its value row.</summary>
    public object? LeadingContent { get => this.GetValue(LeadingContentProperty); set => this.SetValue(LeadingContentProperty, value); }

    /// <summary>Gets the layout selected during measurement.</summary>
    public PropertyLayout ActualLayout { get; private set; }

    private static DependencyProperty RegisterLayoutProperty(string name, Type type, object? defaultValue)
        => DependencyProperty.Register(name, type, typeof(PropertyCard), new PropertyMetadata(defaultValue, (d, _) =>
        {
            var card = (PropertyCard)d;
            card.SynchronizeScalarEditor();
            card.appliedLayout = null;
            card.InvalidateMeasure();
        }));

    /// <summary>The name of the visual state group for common states.</summary>
    public const string CommonStates = "CommonStates";

    /// <summary>The visual state name for the normal state.</summary>
    public const string NormalState = "Normal";

    /// <summary>The visual state name for the pointer-over state.</summary>
    public const string PointerOverState = "PointerOver";

    /// <summary>The visual state name for the disabled state.</summary>
    public const string DisabledState = "Disabled";

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="PropertyName"/> property.
    /// </summary>
    public static readonly DependencyProperty PropertyNameProperty =
        DependencyProperty.Register(
            nameof(PropertyName),
            typeof(string),
            typeof(PropertyCard),
            new PropertyMetadata(default(string)));

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyCard"/> class.
    /// </summary>
    public PropertyCard()
    {
        this.DefaultStyleKey = typeof(PropertyCard);
        _ = this.RegisterPropertyChangedCallback(ContentProperty, static (d, _) => ((PropertyCard)d).SynchronizeScalarEditor());
        _ = this.RegisterPropertyChangedCallback(ContentTemplateProperty, static (d, _) => ((PropertyCard)d).SynchronizeScalarEditor());
    }

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
        this.prefix = this.GetTemplateChild("ValuePrefix") as TextBlock;
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
    protected override Size MeasureOverride(Size availableSize)
    {
        if (this.layoutRoot is not null && this.header is not null && this.editor is not null
            && this.headerQualifier is not null && this.suffix is not null && this.prefix is not null)
        {
            if (this.MeasureScalarEditor(availableSize))
            {
                return base.MeasureOverride(availableSize);
            }

            var scale = Math.Max(this.uiSettings.TextScaleFactor, this.FontSize / 14);
            this.editor.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var annotation = new TextBlock { Text = this.Qualifier, FontSize = 12, FontFamily = this.FontFamily };
            annotation.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var prefixMeasure = new TextBlock { Text = this.Prefix, FontSize = 12, FontFamily = this.FontFamily };
            prefixMeasure.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var annotationWidth = string.IsNullOrEmpty(this.Qualifier) ? 0 : Math.Max(this.QualifierMinimumWidth, annotation.DesiredSize.Width) + 4;
            var prefixWidth = string.IsNullOrEmpty(this.Prefix) ? 0 : prefixMeasure.DesiredSize.Width + 4;
            if (this.LeadingContent is not null)
            {
                this.leading?.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            }

            var accessoryWidth = this.LeadingContent is null ? 0 : (this.leading?.DesiredSize.Width ?? 0) + 4;
            var minimum = Math.Max(this.EditorMinimumWidth * scale, this.editor.DesiredSize.Width) + annotationWidth + prefixWidth + accessoryWidth;
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
            this.suffix.MinWidth = this.IsCompound ? 0 : this.QualifierMinimumWidth;
            this.prefix.Visibility = string.IsNullOrEmpty(this.Prefix) ? Visibility.Collapsed : Visibility.Visible;
            this.leading?.Visibility = this.LeadingContent is null ? Visibility.Collapsed : Visibility.Visible;

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
                var valueGroup = (FrameworkElement)this.editor.Parent;
                Grid.SetRow(valueGroup, layout == PropertyLayout.Inline ? 0 : 1);
                Grid.SetColumn(valueGroup, layout == PropertyLayout.Inline ? 1 : 0);
                this.appliedLayout = layout;
            }
        }

        return base.MeasureOverride(availableSize);
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
