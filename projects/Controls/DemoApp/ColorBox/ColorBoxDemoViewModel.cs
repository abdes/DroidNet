// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// ViewModel for the <see cref="ColorBoxDemoView" /> view.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class ColorBoxDemoViewModel : ObservableObject
{
    private const int MaximumLogLines = 12;

    [ObservableProperty]
    public partial double BaseColorRed { get; set; } = 0.8;

    [ObservableProperty]
    public partial double BaseColorGreen { get; set; } = 0.35;

    [ObservableProperty]
    public partial double BaseColorBlue { get; set; } = 0.1;

    [ObservableProperty]
    public partial double BaseColorAlpha { get; set; } = 1;

    [ObservableProperty]
    public partial double EmissiveRed { get; set; } = 8;

    [ObservableProperty]
    public partial double EmissiveGreen { get; set; } = 4;

    [ObservableProperty]
    public partial double EmissiveBlue { get; set; } = 1;

    [ObservableProperty]
    public partial double HighlightRed { get; set; } = 0.2;

    [ObservableProperty]
    public partial double HighlightGreen { get; set; } = 0.6;

    [ObservableProperty]
    public partial double HighlightBlue { get; set; } = 1;

    [ObservableProperty]
    public partial double HighlightAlpha { get; set; } = 0.5;

    [ObservableProperty]
    public partial double ScaleRed { get; set; } = 1;

    [ObservableProperty]
    public partial double ScaleGreen { get; set; } = 0.9;

    [ObservableProperty]
    public partial double ScaleBlue { get; set; } = 0.8;

    [ObservableProperty]
    public partial double MixedRed { get; set; } = 0.5;

    [ObservableProperty]
    public partial double MixedGreen { get; set; } = 0.5;

    [ObservableProperty]
    public partial double MixedBlue { get; set; } = 0.5;

    [ObservableProperty]
    public partial bool IsMixed { get; set; } = true;

    [ObservableProperty]
    public partial double TintRed { get; set; } = 1;

    [ObservableProperty]
    public partial double TintGreen { get; set; } = 1;

    [ObservableProperty]
    public partial double TintBlue { get; set; } = 1;

    /// <summary>Gets the most recent edit sessions and picks of the logged box, newest first.</summary>
    public ObservableCollection<string> SessionLog { get; } = [];

    /// <summary>Applies a picked tint as one model edit, the way an editor applies a pick to its document.</summary>
    /// <param name="color">The picked channels.</param>
    public void ApplyTint(ColorChannels color)
    {
        (this.TintRed, this.TintGreen, this.TintBlue) = (color.Red, color.Green, color.Blue);
        this.Log(string.Create(CultureInfo.InvariantCulture, $"Picked {color.Red:0.###}, {color.Green:0.###}, {color.Blue:0.###}"));
    }

    /// <summary>Adds a line to <see cref="SessionLog"/>.</summary>
    /// <param name="line">The line to add.</param>
    public void Log(string line)
    {
        this.SessionLog.Insert(0, line);
        while (this.SessionLog.Count > MaximumLogLines)
        {
            this.SessionLog.RemoveAt(this.SessionLog.Count - 1);
        }
    }
}
