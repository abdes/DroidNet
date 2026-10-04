// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Result of an asynchronous tree-item rename request.</summary>
/// <param name="Succeeded">Whether the owner accepted and committed the name.</param>
/// <param name="ErrorMessage">Optional user-facing validation or command failure.</param>
public sealed record TreeItemRenameResult(bool Succeeded, string? ErrorMessage = null)
{
    /// <summary>Gets a successful rename result.</summary>
    public static TreeItemRenameResult Success { get; } = new(Succeeded: true);

    /// <summary>Creates a rejected rename result.</summary>
    /// <param name="message">The reason the name was rejected.</param>
    /// <returns>The rejected result.</returns>
    public static TreeItemRenameResult Rejected(string message) => new(Succeeded: false, message);
}
