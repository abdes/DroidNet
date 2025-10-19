// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Config.Security;

/// <summary>
/// Provides non-generic helper methods for creating and extracting <see cref="Secret{T}"/> instances
/// without declaring static members on the generic type.
/// </summary>
public static class Secret
{
    /// <summary>
    /// Wraps a value in a <see cref="Secret{T}"/> instance.
    /// </summary>
    /// <typeparam name="T">The type of the value to wrap.</typeparam>
    /// <param name="value">The value to wrap as a secret.</param>
    /// <returns>A <see cref="Secret{T}"/> containing the specified value.</returns>
    public static Secret<T> ToSecret<T>(T value) => new(value);

    /// <summary>
    /// Extracts the underlying value from a <see cref="Secret{T}"/> instance.
    /// </summary>
    /// <typeparam name="T">The type of the secret value.</typeparam>
    /// <param name="secret">The <see cref="Secret{T}"/> instance to extract the value from.</param>
    /// <returns>The underlying value contained in the secret.</returns>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="secret"/> is <c>null</c>.</exception>
    public static T FromSecret<T>(Secret<T> secret)
        => (secret ?? throw new ArgumentNullException(nameof(secret))).Value;
}
