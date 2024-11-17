// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Represents a unique identifier for a theme.
/// </summary>
/// <param name="Id">The unique identifier value.</param>
/// <remarks>
/// <para>
/// This struct is used to uniquely identify themes within the application. It ensures that each theme
/// can be referenced and managed consistently.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this struct to define and manage unique identifiers for themes. This can be useful for applying
/// specific themes to different parts of the application or for switching themes dynamically.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var themeId = new ThemeId(1);
/// var theme = Themes.Builtin[themeId];
/// // Apply the theme to a control
/// ]]></code>
/// </para>
/// </remarks>
public readonly record struct ThemeId(uint Id);
