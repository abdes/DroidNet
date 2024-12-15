// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;

namespace DroidNet.Controls;

/// <summary>
/// Parses and formats numeric values based on a specified mask.
/// </summary>
public partial class MaskParser
{
    private readonly int beforeDecimalCount;
    private readonly int afterDecimalCount;
    private readonly bool hasSpace;
    private readonly string unit;
    private readonly bool isUnbounded;
    private readonly int signRequirement;
    private readonly float? maxValue;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaskParser"/> class with the specified mask.
    /// </summary>
    /// <param name="mask">The mask to use for parsing and formatting values.</param>
    /// <exception cref="ArgumentException">Thrown when the mask format is invalid.</exception>
    public MaskParser(string mask)
    {
        // We must either have digits before the dot, or after it, but not no digits at all
        if (mask.Equals(".", StringComparison.Ordinal))
        {
            throw new ArgumentException("Invalid mask format", nameof(mask));
        }

        var regex = MaskRegex();
        var match = regex.Match(mask);

        if (!match.Success)
        {
            throw new ArgumentException("Invalid mask format", nameof(mask));
        }

        var sign = match.Groups["sign"].Value;
        this.signRequirement = sign switch
        {
            "-" => -1,
            "+" => 1,
            _ => 0,
        };

        var beforeDecimal = match.Groups["beforeDecimal"].Value;
        this.isUnbounded = string.Equals(beforeDecimal, "~", StringComparison.Ordinal);
        this.beforeDecimalCount = this.isUnbounded ? 0 : beforeDecimal.Length;
        this.afterDecimalCount = match.Groups["afterDecimal"].Value.Length;
        this.hasSpace = !string.IsNullOrEmpty(match.Groups["space"].Value);
        this.unit = match.Groups["unit"].Value;

        if (!this.isUnbounded)
        {
            this.maxValue = this.beforeDecimalCount > 0
                ? (float)Math.Pow(10, this.beforeDecimalCount) - (float)(1.0 / Math.Pow(10, this.afterDecimalCount))
                : (float)(1.0 - (1.0 / Math.Pow(10, this.afterDecimalCount)));
        }
    }

    /// <summary>
    /// Determines whether the specified value is valid according to the mask.
    /// </summary>
    /// <param name="value">The value to validate.</param>
    /// <returns><see langword="true"/> if the value is valid; otherwise, <see langword="false"/>.</returns>
    public bool IsValidValue(float value) =>
        (this.signRequirement == 0 || Math.Sign(value) == this.signRequirement)
        && (this.isUnbounded || Math.Abs(value) <= this.maxValue);

    /// <summary>
    /// Formats the specified value according to the mask.
    /// </summary>
    /// <param name="value">The value to format.</param>
    /// <param name="withPadding">Whether to pad the formatted value with zeros.</param>
    /// <returns>The formatted value as a string.</returns>
    public string FormatValue(float value, bool withPadding = false)
    {
        // Clamp value to max if not unbounded
        if (!this.isUnbounded && Math.Abs(value) > this.maxValue)
        {
            value = Math.Sign(value) * this.maxValue.Value;
        }

        // Round the value to afterDecimalCount digits
        value = (float)Math.Round(value, this.afterDecimalCount);

        // Create format string for exact digit count
        var beforeFormat = new string(withPadding ? '0' : '#', this.beforeDecimalCount);
        var afterFormat = this.afterDecimalCount > 0 ? new string('0', this.afterDecimalCount) : string.Empty;
        var numberFormat = this.afterDecimalCount > 0 ? $"{beforeFormat}.{afterFormat}" : beforeFormat;

        var result = value.ToString(numberFormat, System.Globalization.CultureInfo.InvariantCulture);

        // Ensure proper handling of 0 value
        if (value == 0 && !result.StartsWith('0'))
        {
            result = "0" + result;
        }

        if (this.hasSpace)
        {
            result += " ";
        }

        if (!string.IsNullOrEmpty(this.unit))
        {
            result += this.unit;
        }

        return result;
    }

    /// <summary>
    /// Gets the regular expression used to parse the mask.
    /// </summary>
    /// <returns>The regular expression for parsing the mask.</returns>
    [GeneratedRegex(
        @"^(?<sign>[-\+])?(?:(?<beforeDecimal>~|\#+)?)\.(?<afterDecimal>\#*)(?<space>\s*)(?<unit>\S+)?$",
        RegexOptions.ExplicitCapture | RegexOptions.Compiled | RegexOptions.CultureInvariant,
        matchTimeoutMilliseconds: 1000)]
    private static partial Regex MaskRegex();
}
