// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// The preview settings of the document editors that show a scene: engine presentation, log verbosity and the ground
/// grid.
/// </summary>
public sealed partial class PreviewSettingsPanel : UserControl
{
    /// <summary>Identifies the <see cref="ViewModel"/> dependency property.</summary>
    public static readonly DependencyProperty ViewModelProperty = DependencyProperty.Register(
        nameof(ViewModel),
        typeof(PreviewSettingsViewModel),
        typeof(PreviewSettingsPanel),
        new PropertyMetadata(defaultValue: null, OnViewModelChanged));

    /// <summary>Initializes a new instance of the <see cref="PreviewSettingsPanel"/> class.</summary>
    public PreviewSettingsPanel() => this.InitializeComponent();

    /// <summary>Gets or sets the preview settings this panel edits.</summary>
    public PreviewSettingsViewModel? ViewModel
    {
        get => (PreviewSettingsViewModel?)this.GetValue(ViewModelProperty);
        set => this.SetValue(ViewModelProperty, value);
    }

    private static void OnViewModelChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((PreviewSettingsPanel)sender).Bindings?.Update();
}
