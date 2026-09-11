// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Uses WinUI semantic theme resources for a cook or asset status.</summary>
public sealed partial class CookingStatusIndicator : UserControl
{
    /// <summary>Identifies an optional asset-specific status caption.</summary>
    public static readonly DependencyProperty CaptionProperty = DependencyProperty.Register(
        nameof(Caption), typeof(string), typeof(CookingStatusIndicator), new PropertyMetadata(string.Empty, OnAppearanceChanged));

    /// <summary>Identifies an optional asset-specific status glyph.</summary>
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph), typeof(string), typeof(CookingStatusIndicator), new PropertyMetadata(string.Empty, OnAppearanceChanged));

    /// <summary>Identifies the execution-state dependency property.</summary>
    public static readonly DependencyProperty StateProperty = DependencyProperty.Register(
        nameof(State), typeof(CookRunState), typeof(CookingStatusIndicator), new PropertyMetadata(CookRunState.Queued, OnAppearanceChanged));

    /// <summary>Identifies whether the status is presented as a text pill.</summary>
    public static readonly DependencyProperty ShowLabelProperty = DependencyProperty.Register(
        nameof(ShowLabel), typeof(bool), typeof(CookingStatusIndicator), new PropertyMetadata(defaultValue: false, OnAppearanceChanged));

    /// <summary>Identifies whether the asset reused existing output.</summary>
    public static readonly DependencyProperty IsReusedProperty = DependencyProperty.Register(
        nameof(IsReused), typeof(bool), typeof(CookingStatusIndicator), new PropertyMetadata(defaultValue: false, OnAppearanceChanged));

    /// <summary>Initializes a new instance of the <see cref="CookingStatusIndicator"/> class.</summary>
    public CookingStatusIndicator()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.UpdateAppearance();
    }

    /// <summary>Gets or sets the execution state.</summary>
    public CookRunState State { get => (CookRunState)this.GetValue(StateProperty); set => this.SetValue(StateProperty, value); }

    /// <summary>Gets or sets a value indicating whether the label pill replaces the standalone icon.</summary>
    public bool ShowLabel { get => (bool)this.GetValue(ShowLabelProperty); set => this.SetValue(ShowLabelProperty, value); }

    /// <summary>Gets or sets a value indicating whether the asset reused validated output.</summary>
    public bool IsReused { get => (bool)this.GetValue(IsReusedProperty); set => this.SetValue(IsReusedProperty, value); }

    /// <summary>Gets or sets an asset-specific caption when its outcome differs from the overall run.</summary>
    public string Caption { get => (string)this.GetValue(CaptionProperty); set => this.SetValue(CaptionProperty, value); }

    /// <summary>Gets or sets an asset-specific status glyph.</summary>
    public string Glyph { get => (string)this.GetValue(GlyphProperty); set => this.SetValue(GlyphProperty, value); }

    private static void OnAppearanceChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((CookingStatusIndicator)sender).UpdateAppearance();

    private void UpdateAppearance()
    {
        var label = !string.IsNullOrEmpty(this.Caption) ? this.Caption : this.IsReused ? "Reused" : CookingRunViewModel.GetStatus(this.State);
        this.StatusIcon.Glyph = !string.IsNullOrEmpty(this.Glyph) ? this.Glyph : this.IsReused ? "\uE72C" : CookingRunViewModel.GetGlyph(this.State);
        this.StatusIcon.Visibility = this.ShowLabel ? Visibility.Collapsed : Visibility.Visible;
        this.StatusPill.Visibility = this.ShowLabel ? Visibility.Visible : Visibility.Collapsed;
        this.StatusLabel.Text = label;
        AutomationProperties.SetName(this, label);
        ToolTipService.SetToolTip(this, label);
        var tone = this.IsReused ? "Neutral" : this.State switch
        {
            CookRunState.Failed => "Critical",
            CookRunState.NeedsSave or CookRunState.SucceededWithWarnings => "Caution",
            CookRunState.Succeeded or CookRunState.UpToDate => "Success",
            _ => "Neutral",
        };
        if (this.IsLoaded)
        {
            _ = VisualStateManager.GoToState(this, tone, useTransitions: false);
        }
    }
}
