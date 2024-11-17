// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Provides access to predefined themes for styling log output.
/// </summary>
/// <remarks>
/// <para>
/// This static class contains predefined themes that can be used to style log output consistently.
/// Each theme is identified by a unique <see cref="ThemeId"/> and can be accessed through the <see cref="Builtin"/> property.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use these predefined themes to apply consistent styling to log output. You can also create custom themes
/// by defining your own mappings of <see cref="ThemeStyle"/> to <see cref="RunStyle"/>.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var theme = Themes.Builtin[Themes.Literate];
/// var run = new Run { Text = "Information: Operation completed successfully." };
/// theme.Apply(paragraph, ThemeStyle.Information);
/// // The run is added to the paragraph with the specified information styling
/// ]]></code>
/// </para>
/// </remarks>
public static class OutputLogThemes
{
    /// <summary>
    /// Gets the theme identifier for the "Empty" theme, which does not apply any styles.
    /// </summary>
    public static readonly ThemeId Empty = new(0);

    /// <summary>
    /// Gets the theme identifier for the "Literate" theme, which uses a light-on-dark color scheme.
    /// </summary>
    public static readonly ThemeId Literate = new(1);

    /// <summary>
    /// Gets the theme identifier for the "Grayscale" theme, which uses shades of gray for styling.
    /// </summary>
    public static readonly ThemeId Grayscale = new(2);

    /// <summary>
    /// Gets the theme identifier for the "Colored" theme, which uses a colorful palette for styling.
    /// </summary>
    public static readonly ThemeId Colored = new(3);

    private static readonly Lazy<IReadOnlyDictionary<ThemeId, Theme>> LazyBuiltin
        = new(
            () => new Dictionary<ThemeId, Theme>
            {
                [Empty] = new EmptyTheme(),
                [Literate] = RichTextThemeDefinitions.Literate,
                [Grayscale] = RichTextThemeDefinitions.Grayscale,
                [Colored] = RichTextThemeDefinitions.Colored,
            });

    /// <summary>
    /// Gets the collection of built-in themes.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The collection is lazily initialized so that the logger configuration, which runs on a non-UI thread, will not create any
    /// issues with wrong thread execution before the UI is loaded.
    /// </para>
    /// </remarks>
    public static IReadOnlyDictionary<ThemeId, Theme> Builtin => LazyBuiltin.Value;
}
