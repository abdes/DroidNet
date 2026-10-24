// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The theme brushes a console paints its rows with, resolved once per theme so no row allocates a brush.
/// </summary>
/// <param name="Primary">Information message text.</param>
/// <param name="Secondary">Information tags, subsystems and fold counts.</param>
/// <param name="Tertiary">Prefix brackets and times, and Verbose and Debug text.</param>
/// <param name="Caution">Warning text, and the evicting buffer ring.</param>
/// <param name="Critical">Error and Fatal text, and the exception glyph.</param>
/// <param name="Accent">The buffer ring.</param>
/// <param name="CurrentMatchBackground">The current find match background: WinUI's selected-text background.</param>
/// <param name="CurrentMatchForeground">The current find match text: WinUI's selected-text foreground.</param>
/// <param name="MatchBackground">The background of other search matches: a subtle fill.</param>
/// <param name="MatchForeground">The text of other search matches: the accent text color.</param>
/// <param name="BadgeBackground">The fold badge background.</param>
internal sealed record ConsoleBrushes(
    Brush Primary,
    Brush Secondary,
    Brush Tertiary,
    Brush Caution,
    Brush Critical,
    Brush Accent,
    Brush CurrentMatchBackground,
    Brush CurrentMatchForeground,
    Brush MatchBackground,
    Brush MatchForeground,
    Brush BadgeBackground)
{
    /// <summary>
    ///     Resolves the brushes from the application's theme resources.
    /// </summary>
    /// <param name="fallback">The brush used for any resource that cannot be found.</param>
    /// <returns>The brushes.</returns>
    public static ConsoleBrushes Resolve(Brush fallback)
    {
        var resources = Application.Current?.Resources;
        return new ConsoleBrushes(
            Get("TextFillColorPrimaryBrush"),
            Get("TextFillColorSecondaryBrush"),
            Get("TextFillColorTertiaryBrush"),
            Get("SystemFillColorCautionBrush"),
            Get("SystemFillColorCriticalBrush"),
            Get("AccentFillColorDefaultBrush"),
            Get("AccentFillColorSelectedTextBackgroundBrush"),
            Get("TextOnAccentFillColorSelectedTextBrush"),
            Get("SubtleFillColorSecondaryBrush"),
            Get("AccentTextFillColorPrimaryBrush"),
            Get("ControlFillColorSecondaryBrush"));

        Brush Get(string key)
            => resources is not null && resources.TryGetValue(key, out var value) && value is Brush brush ? brush : fallback;
    }

    /// <summary>
    ///     Gets the brush of a level's tag.
    /// </summary>
    /// <param name="level">The level.</param>
    /// <returns>The brush.</returns>
    public Brush TagFor(LogEventLevel level) => level switch
    {
        LogEventLevel.Verbose or LogEventLevel.Debug => this.Tertiary,
        LogEventLevel.Warning => this.Caution,
        LogEventLevel.Error or LogEventLevel.Fatal => this.Critical,
        _ => this.Secondary,
    };

    /// <summary>
    ///     Gets the brush of a level's message text.
    /// </summary>
    /// <param name="level">The level.</param>
    /// <returns>The brush.</returns>
    public Brush MessageFor(LogEventLevel level) => level switch
    {
        LogEventLevel.Verbose or LogEventLevel.Debug => this.Tertiary,
        LogEventLevel.Warning => this.Caution,
        LogEventLevel.Error or LogEventLevel.Fatal => this.Critical,
        _ => this.Primary,
    };
}
