// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Rendering;

/// <summary>
/// Provides methods for applying padding transformations to strings based on alignment settings.
/// </summary>
/// <remarks>
/// <para>
/// This class contains methods that simplify the manipulation of string padding, such as applying
/// left or right padding based on specified alignment settings.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// These methods are designed to be used in conjunction with rendering operations that require
/// consistent padding transformations. Ensure that the alignment values are appropriate for the context
/// in which they are used to avoid formatting issues.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var paddedValue = PaddingTransform.Apply("Hello", new Alignment(AlignmentDirection.Right, 10));
/// // paddedValue is "     Hello"
/// ]]></code>
/// </para>
/// </remarks>
internal static class PaddingTransform
{
    private static readonly char[] PaddingChars = new string(' ', 80).ToCharArray();

    /// <summary>
    /// Writes the provided value to the output, applying direction-based padding when <paramref name="alignment"/> is provided.
    /// </summary>
    /// <param name="value">The value to pad.</param>
    /// <param name="alignment">The alignment settings to apply when rendering <paramref name="value"/>.</param>
    /// <returns>The resulting string with padding applied.</returns>
    /// <remarks>
    /// <para>
    /// This method applies padding to the provided value based on the specified alignment settings.
    /// If the alignment direction is <see cref="AlignmentDirection.Left"/>, padding is added to the right.
    /// If the alignment direction is <see cref="AlignmentDirection.Right"/>, padding is added to the left.
    /// </para>
    /// <para>
    /// <strong>Corner Cases:</strong>
    /// If the value length is greater than or equal to the alignment width, the original value is returned unchanged.
    /// </para>
    /// </remarks>
    public static string Apply(string value, Alignment? alignment)
    {
        if (alignment is null || value.Length >= alignment.Value.Width)
        {
            return value;
        }

        var pad = alignment.Value.Width - value.Length;
        using var output = new StringWriter();

        if (alignment.Value.Direction == AlignmentDirection.Left)
        {
            output.Write(value);
        }

        if (pad <= PaddingChars.Length)
        {
            output.Write(PaddingChars, 0, pad);
        }
        else
        {
            output.Write(new string(' ', pad));
        }

        if (alignment.Value.Direction == AlignmentDirection.Right)
        {
            output.Write(value);
        }

        return output.ToString();
    }
}
