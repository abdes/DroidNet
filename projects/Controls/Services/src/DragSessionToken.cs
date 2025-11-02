// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Opaque token representing an active drag visual session.
/// </summary>
public readonly struct DragSessionToken : IEquatable<DragSessionToken>
{
    /// <summary>
    ///     Gets the opaque identifier for the token.
    /// </summary>
    public Guid Id { get; init; }

    /// <summary>
    ///     Equality operator for <see cref="DragSessionToken"/>.
    /// </summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns><see langword="true"/> if equal; otherwise <see langword="false"/>.</returns>
    public static bool operator ==(DragSessionToken left, DragSessionToken right) => left.Equals(right);

    /// <summary>
    ///     Inequality operator for <see cref="DragSessionToken"/>.
    /// </summary>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    /// <returns><see langword="true"/> if not equal; otherwise <see langword="false"/>.</returns>
    public static bool operator !=(DragSessionToken left, DragSessionToken right) => !left.Equals(right);

    /// <inheritdoc/>
    public override string ToString() => this.Id.ToString("D");

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is DragSessionToken token && this.Equals(token);

    /// <inheritdoc/>
    public bool Equals(DragSessionToken other) => this.Id.Equals(other.Id);

    /// <inheritdoc/>
    public override int GetHashCode() => this.Id.GetHashCode();
}
