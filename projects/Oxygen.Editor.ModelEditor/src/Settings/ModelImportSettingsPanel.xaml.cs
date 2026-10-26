// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>The import settings panel: a stack of property sections of the editor's shared property editors.</summary>
public sealed partial class ModelImportSettingsPanel : UserControl
{
    /// <summary>Identifies the <see cref="ViewModel"/> property.</summary>
    public static readonly DependencyProperty ViewModelProperty = DependencyProperty.Register(
        nameof(ViewModel), typeof(ModelImportSettingsPanelViewModel), typeof(ModelImportSettingsPanel), new PropertyMetadata(defaultValue: null));

    /// <summary>Initializes a new instance of the <see cref="ModelImportSettingsPanel"/> class.</summary>
    public ModelImportSettingsPanel()
    {
        this.InitializeComponent();
    }

    /// <summary>Gets or sets the panel's view model.</summary>
    public ModelImportSettingsPanelViewModel? ViewModel
    {
        get => (ModelImportSettingsPanelViewModel?)this.GetValue(ViewModelProperty);
        set => this.SetValue(ViewModelProperty, value);
    }

    /// <summary>Shows a row only when it has text, such as the detected units of one analyzed file.</summary>
    /// <param name="text">The row's text.</param>
    /// <returns>Visible when the text is not empty.</returns>
    internal static Visibility ShownWhenSet(string? text) => string.IsNullOrEmpty(text) ? Visibility.Collapsed : Visibility.Visible;

    private void OnMaterialChoiceInvoked(object? sender, AssetChoice choice)
    {
        if (sender is FrameworkElement { DataContext: MaterialRow row })
        {
            row.Choose(choice);
        }
    }
}
