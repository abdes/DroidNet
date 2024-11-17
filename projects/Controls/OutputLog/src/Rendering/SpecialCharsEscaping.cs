// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security;

namespace DroidNet.Controls.OutputLog.Rendering;

/// <summary>
/// Provides methods for escaping special characters in strings.
/// </summary>
/// <remarks>
/// <para>
/// This class contains methods that simplify the escaping of special characters in strings,
/// ensuring that the strings are safe for use in various contexts such as XML or JSON.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// These methods are designed to be used in conjunction with rendering operations that require
/// consistent escaping of special characters. Ensure that the strings are properly escaped to avoid
/// issues with invalid characters in the output.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var escapedValue = SpecialCharsEscaping.Apply("Hello & Welcome <World>");
/// // escapedValue is "Hello &amp; Welcome &lt;World&gt;"
/// ]]></code>
/// </para>
/// </remarks>
internal static class SpecialCharsEscaping
{
    /// <summary>
    /// Escapes special characters in the given string.
    /// </summary>
    /// <param name="value">The string to escape.</param>
    /// <returns>The escaped string with special characters replaced by their corresponding escape sequences.</returns>
    /// <remarks>
    /// <para>
    /// This method uses <see cref="SecurityElement.Escape"/> to replace special characters such as
    /// '&lt;', '&gt;', '&amp;', and '"' with their corresponding escape sequences.
    /// </para>
    /// <para>
    /// <strong>Corner Cases:</strong>
    /// If the input string is <see langword="null"/>, an empty string is returned.
    /// </para>
    /// </remarks>
    public static string Apply(string value) => SecurityElement.Escape(value);
}
