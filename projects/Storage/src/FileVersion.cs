// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Storage;

/// <summary>A content identity used to reject writes against a changed destination.</summary>
/// <param name="Exists">Whether the file existed when captured.</param>
/// <param name="Sha256">The SHA-256 digest of its complete bytes.</param>
public sealed record FileVersion(bool Exists, string Sha256)
{
    /// <summary>Gets the expectation for a new destination that must not be overwritten.</summary>
    public static FileVersion Missing { get; } = new(Exists: false, string.Empty);
}
