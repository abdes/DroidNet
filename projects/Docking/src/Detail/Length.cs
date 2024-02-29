// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Globalization;
using System.Text.RegularExpressions;

/// <summary>
/// Encapsulates a length value as a <see langword="string"/> compatible with <see
/// href="https://learn.microsoft.com/en-us/dotnet/api/system.windows.gridlength?view=windowsdesktop-8.0">
/// GridLength</see> format.
/// </summary>
/// <remarks>
/// This is an abstract base class, that should be specialized to represent specific dimensions such as width or height, while
/// avoiding the errors causes by multiple dimensions with the same type appearing as arguments to methods.
/// </remarks>
/// <seealso cref="Width"/>
/// <seealso cref="Height"/>
public abstract partial class Length
{
    private readonly string? value;

    /// <summary>
    /// Initializes a new instance of the <see cref="Length"/> class with a string value.
    /// </summary>
    /// <param name="value">The value as a string, which can be null. A <see langword="null"/> value indicates the absence of
    /// any requirement regarding this dimension. When not <see langword="null"/>, the value must be of one of the following
    /// formats:
    /// <list type="bullet">
    /// <item><term>auto</term>
    /// <description>value should be determined automatically.</description>
    /// </item>
    /// <item><term>numeric</term>
    /// <description>an explicit value as a <see langword="double"/>.</description>
    /// </item>
    /// <item><term>star</term>
    /// <description>the value is expressed as a weighted proportion of the available space, specified as a <see
    /// langword="double"/> followed with the character '<c>*</c>'. A lone star is equivalent to "1*".</description>
    /// </item>
    /// </list>
    /// </param>
    /// <exception cref="ArgumentException">when the provided non-null value does not respect the expected format.</exception>
    protected Length(string? value)
    {
        if (value != null && !GridLengthRegEx().IsMatch(value))
        {
            throw new ArgumentException("length value {value} is not compatible with GridLength format", nameof(value));
        }

        this.value = value;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="Length"/> class with a specific numeric value as pixels.
    /// </summary>
    /// <param name="pixels">
    /// The number of device-independent pixels (96 pixels-per-inch), rounded to the nearest integer.
    /// </param>
    protected Length(double pixels) => this.value = double.Round(pixels).ToString(CultureInfo.InvariantCulture);

    /// <summary>
    /// Gets a value indicating whether the underlying value in this <see cref="Length"/> is <see langword="null"/> or is an empty
    /// string.
    /// </summary>
    public bool IsNullOrEmpty => string.IsNullOrEmpty(this.value);

    /// <summary>
    /// Implicitly convert the underlying value of a <see cref="Length"/> to a string.
    /// </summary>
    /// <param name="length">the length to be converted to a string.</param>
    public static implicit operator string?(Length? length) => length?.value;

    public override string ToString() => this.value ?? "?";

    [GeneratedRegex(@"^(auto|(\d+(\.\d+)?)?\*?)$")]
    private static partial Regex GridLengthRegEx();
}
