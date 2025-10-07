// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents a strongly typed, zero-based menu level within a hierarchical menu surface.
/// </summary>
/// <remarks>
///     This type wraps an <see cref="int"/> to provide semantic clarity and type safety when working with menu levels.
///     It enforces non-negative values and supports common operations such as incrementing, decrementing, and
///     conversion to and from <see cref="int"/>.
/// </remarks>
public readonly record struct MenuLevel
{
    /// <summary>
    ///     A special <see cref="MenuLevel"/> representing the first level of a menu (level 0).
    /// </summary>
    public static readonly MenuLevel First = new(0);

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuLevel"/> struct.
    /// </summary>
    /// <param name="value">The zero-based integer value representing the menu level.</param>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="value"/> is negative.
    /// </exception>
    public MenuLevel(int value)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(value);
        this.Value = value;
    }

    /// <summary>
    ///     Gets the underlying integer value of this menu level.
    /// </summary>
    public int Value { get; }

    /// <summary>
    ///     Gets a value indicating whether this <see cref="MenuLevel"/> represents the first (level 0).
    /// </summary>
    public bool IsFirst => this.Value == First.Value;

    /// <summary>
    ///     Defines an implicit conversion from <see cref="MenuLevel"/> to <see cref="int"/>.
    /// </summary>
    /// <param name="level">The <see cref="MenuLevel"/> to convert.</param>
    /// <returns>The underlying integer value of the specified <paramref name="level"/>.</returns>
    public static implicit operator int(MenuLevel level) => level.Value;

    /// <summary>
    ///     Defines an explicit conversion from <see cref="int"/> to <see cref="MenuLevel"/>.
    /// </summary>
    /// <param name="value">The integer value to convert.</param>
    /// <returns>A new <see cref="MenuLevel"/> representing the specified <paramref name="value"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="value"/> is negative.
    /// </exception>
    public static explicit operator MenuLevel(int value) => new(value);

    /// <summary>
    ///     Increments the specified <see cref="MenuLevel"/> by one.
    /// </summary>
    /// <param name="level">The menu level to increment.</param>
    /// <returns>A new <see cref="MenuLevel"/> one greater than <paramref name="level"/>.</returns>
    public static MenuLevel operator ++(MenuLevel level) => new(level.Value + 1);

    /// <summary>
    ///     Decrements the specified <see cref="MenuLevel"/> by one.
    /// </summary>
    /// <param name="level">The menu level to decrement.</param>
    /// <returns>A new <see cref="MenuLevel"/> one less than <paramref name="level"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown if the resulting value would be negative.
    /// </exception>
    public static MenuLevel operator --(MenuLevel level) => new(level.Value - 1);

    /// <summary>
    ///     Returns a string representation of this <see cref="MenuLevel"/> using invariant culture.
    /// </summary>
    /// <returns>A string that represents the current <see cref="MenuLevel"/>.</returns>
    public override string ToString() => this.Value.ToString(CultureInfo.InvariantCulture);

    /// <summary>
    /// Returns the underlying integer value of this <see cref="MenuLevel"/>.
    /// </summary>
    /// <returns>The integer value of this <see cref="MenuLevel"/>.</returns>
    public int ToInt32() => this.Value;

    /// <summary>
    ///     Creates a new <see cref="MenuLevel"/> from the specified integer value.
    /// </summary>
    /// <param name="value">The integer value to wrap.</param>
    /// <returns>A new <see cref="MenuLevel"/> representing the specified <paramref name="value"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="value"/> is negative.
    /// </exception>
    public static MenuLevel FromInt32(int value) => new(value);

    /// <summary>
    ///     Returns a new <see cref="MenuLevel"/> incremented by one from the specified instance.
    /// </summary>
    /// <param name="item">The menu level to increment.</param>
    /// <returns>A new <see cref="MenuLevel"/> one greater than <paramref name="item"/>.</returns>
    public static MenuLevel Increment(MenuLevel item) => new(item.Value + 1);

    /// <summary>
    ///     Returns a new <see cref="MenuLevel"/> decremented by one from the specified instance.
    /// </summary>
    /// <param name="item">The menu level to decrement.</param>
    /// <returns>A new <see cref="MenuLevel"/> one less than <paramref name="item"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown if the resulting value would be negative.
    /// </exception>
    public static MenuLevel Decrement(MenuLevel item) => new(item.Value - 1);
}
