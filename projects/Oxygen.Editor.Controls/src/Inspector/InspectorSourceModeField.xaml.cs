// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

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
public sealed partial class InspectorSourceModeField : UserControl
{
    /// <summary>Identifies the property caption.</summary>
    public static readonly DependencyProperty LabelProperty = Register<string>(nameof(Label), string.Empty);

    /// <summary>Identifies the mode names, in index order.</summary>
    public static readonly DependencyProperty ModeNamesProperty = Register<IReadOnlyList<string>>(nameof(ModeNames), null);

    /// <summary>Identifies the selected mode's index.</summary>
    public static readonly DependencyProperty SelectedIndexProperty = Register<int>(nameof(SelectedIndex), -1);

    /// <summary>Identifies the caption line's text.</summary>
    public static readonly DependencyProperty CaptionProperty = Register<string>(nameof(Caption), string.Empty, OnPresentationChanged);

    /// <summary>Identifies the source button's tooltip and automation name.</summary>
    public static readonly DependencyProperty SourceToolTipProperty = Register<string>(nameof(SourceToolTip), string.Empty, OnPresentationChanged);

    /// <summary>Initializes a new instance of the <see cref="InspectorSourceModeField"/> class.</summary>
    public InspectorSourceModeField()
    {
        this.InitializeComponent();
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

    private Visibility CaptionVisibility
        => string.IsNullOrEmpty(this.Caption) && string.IsNullOrEmpty(this.SourceToolTip) ? Visibility.Collapsed : Visibility.Visible;

    private Visibility SourceVisibility => string.IsNullOrEmpty(this.SourceToolTip) ? Visibility.Collapsed : Visibility.Visible;

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(InspectorSourceModeField), new PropertyMetadata(defaultValue, changed));

    private static void OnPresentationChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((InspectorSourceModeField)sender).Bindings?.Update();

    private void OnSourceClick(object sender, RoutedEventArgs args) => this.SourceRequested?.Invoke(this, EventArgs.Empty);
}
