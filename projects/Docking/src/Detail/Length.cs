// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;

namespace DroidNet.Docking.Detail;

/// <summary>
/// Encapsulates a length value as a <see langword="string" /> compatible with
/// <see href="https://learn.microsoft.com/en-us/dotnet/api/system.windows.gridlength?view=windowsdesktop-8.0">
/// GridLength</see> format.
/// </summary>
/// <remarks>
/// This is an abstract base class, that should be specialized to represent specific dimensions such
/// as width or height, while avoiding the errors causes by multiple dimensions with the same type
/// appearing as arguments to methods.
/// </remarks>
/// <seealso cref="Width" />
/// <seealso cref="Height" />
public abstract partial class Length
{
    private readonly string? value;

    /// <summary>
    /// Initializes a new instance of the <see cref="Length" /> class with a string value.
    /// </summary>
    /// <param name="value">The value as a string, which can be null. A <see langword="null" />
    /// value indicates the absence of any requirement regarding this dimension. When not
    /// <see langword="null" />, the value must be of one of the following formats:
    /// <list type="bullet">
    /// <item>
    /// <term>auto</term>
    /// <description>value should be determined automatically.</description>
    /// </item>
    /// <item>
    /// <term>numeric</term>
    /// <description>an explicit value as a <see langword="double" />.</description>
    /// </item>
    /// <item>
    /// <term>star</term>
    /// <description>the value is expressed as a weighted proportion of the available space,
    /// specified as a <see langword="double" /> followed with the character '<c>*</c>'. A lone star
    /// is equivalent to "1*".</description>
    /// </item>
    /// </list>
    /// </param>
    /// <exception cref="ArgumentException">when the provided non-null value does not respect the
    /// expected format.</exception>
    protected Length(string? value)
    {
        if (value != null && !GridLengthRegEx().IsMatch(value))
        {
            throw new ArgumentException("length value {value} is not compatible with GridLength format", nameof(value));
        }

        this.value = value;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="Length" /> class with a specific numeric value
    /// as pixels.
    /// </summary>
    /// <param name="pixels">
    /// The number of device-independent pixels (96 pixels-per-inch), rounded to the nearest
    /// integer.
    /// </param>
    protected Length(double pixels)
    {
        this.value = double.Round(pixels).ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>
    /// Gets a value indicating whether the underlying value in this <see cref="Length" /> is
    /// <see langword="null" /> or is an empty string.
    /// </summary>
    public bool IsNullOrEmpty => string.IsNullOrEmpty(this.value);

    /// <summary>
    /// Implicitly convert the underlying value of a <see cref="Length" /> to a string.
    /// </summary>
    /// <param name="length">the length to be converted to a string.</param>
    public static implicit operator string?(Length? length) => length?.value;

    /// <inheritdoc />
    public override string? ToString() => this;

    /// <summary>
    /// Returns half of the length value.
    /// </summary>
    /// <returns>
    /// A string representing half of the length value, or <see langword="null" /> if the
    /// original value is <see langword="null" />.
    /// </returns>
    public string? Half()
    {
        if (this.value is null)
        {
            return null;
        }

        var (numericValue, isStar) = NumericValue(this.value);
        return (numericValue / 2).ToString(CultureInfo.InvariantCulture) + (isStar ? "*" : string.Empty);
    }

    /// <inheritdoc />
    public override bool Equals(object? obj)
    {
        if (obj == null || this.GetType() != obj.GetType())
        {
            return false;
        }

        var other = (Length)obj;
        return string.Equals(this.value, other.value, StringComparison.Ordinal);
    }

    /// <inheritdoc />
    public override int GetHashCode() => this.value?.GetHashCode(StringComparison.Ordinal) ?? 0;

    /// <summary>
    /// Converts the underlying value of this <see cref="Length" /> instance to a debug string.
    /// </summary>
    /// <returns>
    /// A string representation of the underlying value if it is not <see langword="null" />,
    /// otherwise a question mark ("?").
    /// </returns>
    public virtual string ToDebugString() => this.value ?? "?";

    [GeneratedRegex(@"^(auto|(\d+(\.\d+)?)?\*?)$", RegexOptions.ExplicitCapture, matchTimeoutMilliseconds: 1000)]
    private static partial Regex GridLengthRegEx();

    private static (double numericValue, bool isStar) NumericValue(string length)
    {
        Debug.Assert(length is not null, "cannot get numeric value of a length with a null value");

        double numericValue;
        var isStar = false;

        if (length[^1] == '*')
        {
            length = length.Remove(length.Length - 1);
            if (string.IsNullOrEmpty(length))
            {
                numericValue = 1;
            }
            else if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
            {
                throw new ArgumentException(
                    $"the length `{length}` does not contain a valid numeric value",
                    nameof(length));
            }

            isStar = true;
        }
        else if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
        {
            throw new ArgumentException(
                $"the length `{length}` is not a valid absolute length",
                nameof(length));
        }

        return (numericValue, isStar);
    }
}
