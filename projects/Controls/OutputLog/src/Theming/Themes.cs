// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

using System.Diagnostics.CodeAnalysis;

[SuppressMessage(
    "ReSharper",
    "MemberCanBePrivate.Global",
    Justification = "All members are part of the public interface")]
public static class Themes
{
    public static readonly ThemeId Empty = new(0);
    public static readonly ThemeId Literate = new(1);
    public static readonly ThemeId Grayscale = new(2);
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
    /// The collection is lazily initialized so that the logger configuration, which runs on a non-UI thread, will not create any
    /// issues with wrong thread execution before the UI is loaded.
    /// </remarks>
    public static IReadOnlyDictionary<ThemeId, Theme> Builtin => LazyBuiltin.Value;
}
