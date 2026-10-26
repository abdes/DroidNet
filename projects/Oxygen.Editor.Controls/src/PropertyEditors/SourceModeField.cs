// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Oxygen.Editor.Controls;

/// <summary>
///     Composes a source-mode property's caption, its toolkit <c>Segmented</c> modes and a caption line, such as a node
///     flag's Inherit / Shown / Hidden with the value Inherit resolves to.
/// </summary>
/// <remarks>
///     The row lays out as every <see cref="PropertyCard"/> does, inline or stacked by its width. A
///     <see cref="SelectedIndex"/> of -1 selects no mode, the mixed state of a multi-selection. When
///     <see cref="SourceToolTip"/> is not empty, the caption line ends with a button that raises
///     <see cref="SourceRequested"/>, for example to select the ancestor a value comes from. The field never edits a
///     model: the selected index is its only output.
/// </remarks>
[TemplatePart(Name = ModesPartName, Type = typeof(Segmented))]
[TemplatePart(Name = CaptionRowPartName, Type = typeof(FrameworkElement))]
[TemplatePart(Name = SourceButtonPartName, Type = typeof(ButtonBase))]
public sealed partial class SourceModeField : Control
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the mode names, in index order.</summary>
    public static readonly DependencyProperty ModeNamesProperty = Register<IReadOnlyList<string>>(nameof(ModeNames), null, OnModesChanged);

    /// <summary>Identifies the selected mode's index.</summary>
    public static readonly DependencyProperty SelectedIndexProperty = Register<int>(nameof(SelectedIndex), -1, OnModesChanged);

    /// <summary>Identifies the caption line's text.</summary>
    public static readonly DependencyProperty CaptionProperty = Register<string>(nameof(Caption), string.Empty, OnPresentationChanged);

    /// <summary>Identifies the source button's tooltip and automation name.</summary>
    public static readonly DependencyProperty SourceToolTipProperty = Register<string>(nameof(SourceToolTip), string.Empty, OnPresentationChanged);

    private const string ModesPartName = "PART_Modes";
    private const string CaptionRowPartName = "PART_CaptionRow";
    private const string SourceButtonPartName = "PART_SourceButton";

    private Segmented? modes;
    private FrameworkElement? captionRow;
    private ButtonBase? sourceButton;
    private long selectedIndexToken;
    private bool showingModes;

    /// <summary>Initializes a new instance of the <see cref="SourceModeField"/> class.</summary>
    public SourceModeField()
    {
        this.DefaultStyleKey = typeof(SourceModeField);
    }

    /// <summary>Occurs when the user asks for the source the caption names.</summary>
    public event EventHandler? SourceRequested;

    /// <summary>Gets or sets the property caption.</summary>
    public string Label { get => (string)this.GetValue(LabelProperty); set => this.SetValue(LabelProperty, value); }

    /// <summary>Gets or sets the mode names, in index order.</summary>
    public IReadOnlyList<string>? ModeNames { get => (IReadOnlyList<string>?)this.GetValue(ModeNamesProperty); set => this.SetValue(ModeNamesProperty, value); }

    /// <summary>Gets or sets the selected mode's index; -1 selects none, for mixed values.</summary>
    public int SelectedIndex { get => (int)this.GetValue(SelectedIndexProperty); set => this.SetValue(SelectedIndexProperty, value); }

    /// <summary>Gets or sets the caption line's text; the line hides while it is empty.</summary>
    public string Caption { get => (string)this.GetValue(CaptionProperty); set => this.SetValue(CaptionProperty, value); }

    /// <summary>Gets or sets the source button's tooltip; the button hides while it is empty.</summary>
    public string SourceToolTip { get => (string)this.GetValue(SourceToolTipProperty); set => this.SetValue(SourceToolTipProperty, value); }

    /// <summary>Retrieves the template parts, shows the modes and listens to them, detaching from a previous template.</summary>
    protected override void OnApplyTemplate()
    {
        this.modes?.UnregisterPropertyChangedCallback(Selector.SelectedIndexProperty, this.selectedIndexToken);

        this.sourceButton?.Click -= this.OnSourceClick;

        base.OnApplyTemplate();
        this.modes = this.GetTemplateChild(ModesPartName) as Segmented;
        this.captionRow = this.GetTemplateChild(CaptionRowPartName) as FrameworkElement;
        this.sourceButton = this.GetTemplateChild(SourceButtonPartName) as ButtonBase;
        this.ShowModes();
        this.UpdatePresentation();
        if (this.modes is not null)
        {
            this.selectedIndexToken = this.modes.RegisterPropertyChangedCallback(Selector.SelectedIndexProperty, this.OnModeSelected);
        }

        this.sourceButton?.Click += this.OnSourceClick;
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(SourceModeField), new PropertyMetadata(defaultValue, changed));

    private static void OnModesChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((SourceModeField)sender).ShowModes();

    private static void OnPresentationChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((SourceModeField)sender).UpdatePresentation();

    // The modes come first, then the index into them, so a new list never loses the field's selection.
    private void ShowModes()
    {
        if (this.modes is not { } segmented)
        {
            return;
        }

        this.showingModes = true;
        try
        {
            if (!ReferenceEquals(segmented.ItemsSource, this.ModeNames))
            {
                segmented.ItemsSource = this.ModeNames;
            }

            segmented.SelectedIndex = this.SelectedIndex;
        }
        finally
        {
            this.showingModes = false;
        }
    }

    // The caption line shows while it has a caption or a source; the button only with a source.
    private void UpdatePresentation()
    {
        var hasSource = !string.IsNullOrEmpty(this.SourceToolTip);
        this.captionRow?.Visibility = string.IsNullOrEmpty(this.Caption) && !hasSource ? Visibility.Collapsed : Visibility.Visible;
        this.sourceButton?.Visibility = hasSource ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnModeSelected(DependencyObject sender, DependencyProperty property)
    {
        if (!this.showingModes && this.modes is { } segmented)
        {
            this.SelectedIndex = segmented.SelectedIndex;
        }
    }

    private void OnSourceClick(object sender, RoutedEventArgs args) => this.SourceRequested?.Invoke(this, EventArgs.Empty);
}
