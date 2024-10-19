// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Rendering;

using Serilog.Parsing;

internal static class PaddingTransform
{
    private static readonly char[] PaddingChars = new string(' ', 80).ToCharArray();

    /// <summary>
    /// Writes the provided value to the output, applying direction-based padding when <paramref name="alignment" /> is provided.
    /// </summary>
    /// <param name="value">Provided value.</param>
    /// <param name="alignment">The alignment settings to apply when rendering <paramref name="value" />.</param>
    /// <returns>The resulting string with padding applied.</returns>
    public static string Apply(string value, Alignment? alignment)
    {
        if (alignment is null || value.Length >= alignment.Value.Width)
        {
            return value;
        }

        var pad = alignment.Value.Width - value.Length;
        var output = new StringWriter();

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
