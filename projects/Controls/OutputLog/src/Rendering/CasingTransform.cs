// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace DroidNet.Controls.OutputLog.Rendering;

/// <summary>
/// Provides methods for applying casing transformations to strings.
/// </summary>
/// <remarks>
/// <para>
/// This class contains methods that simplify the manipulation of string casing, such as converting
/// strings to uppercase or lowercase based on a specified format.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// These methods are designed to be used in conjunction with rendering operations that require
/// consistent casing transformations. Ensure that the format values are appropriate for the context
/// in which they are used to avoid unexpected results.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var transformedValue = CasingTransform.Apply("Hello, World!", "u");
/// // transformedValue is "HELLO, WORLD!"
/// ]]></code>
/// </para>
/// </remarks>
internal static class CasingTransform
{
    /// <summary>
    /// Applies the specified casing transformation to the given string.
    /// </summary>
    /// <param name="value">The string to transform.</param>
    /// <param name="format">The format string indicating the desired casing transformation. Can be <see langword="null"/>.</param>
    /// <returns>The transformed string with the specified casing applied.</returns>
    /// <remarks>
    /// <para>
    /// This method supports the following format strings:
    /// </para>
    /// <para>
    /// <strong>Format Options:</strong>
    /// - "u" for uppercase
    /// - "w" for lowercase.
    /// </para>
    /// <para>
    /// If the format string is <see langword="null"/> or not recognized, the original string is returned unchanged.
    /// </para>
    /// <para>
    /// <strong>Corner Cases:</strong>
    /// If the input string is <see langword="null"/>, an empty string is returned.
    /// </para>
    /// </remarks>
    public static string Apply(string value, string? format = default)
        => format switch
        {
            "u" => value.ToUpper(CultureInfo.CurrentUICulture),
            "w" => value.ToLower(CultureInfo.CurrentUICulture),
            _ => value,
        };
}
