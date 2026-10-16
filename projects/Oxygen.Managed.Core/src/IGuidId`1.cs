// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>
///     An identity backed by a <see cref="Guid"/> whose kind is part of its type, so one kind of id can never be passed
///     where another is expected.
/// </summary>
/// <typeparam name="TSelf">The id type.</typeparam>
public interface IGuidId<TSelf>
    where TSelf : struct, IGuidId<TSelf>
{
    /// <summary>
    ///     Gets the underlying value. Read it only where the id leaves typed code: serialization and the native boundary.
    /// </summary>
    public Guid Value { get; }

    /// <summary>
    ///     Creates the id that has the given underlying value.
    /// </summary>
    /// <param name="value">The underlying value.</param>
    /// <returns>The id.</returns>
    public static abstract TSelf FromValue(Guid value);
}
