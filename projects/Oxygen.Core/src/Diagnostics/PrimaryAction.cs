// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// UI-independent descriptor for an operation result primary action.
/// </summary>
public sealed record PrimaryAction
{
    /// <summary>
    /// Gets the stable action identity.
    /// </summary>
    public required string ActionId { get; init; }

    /// <summary>
    /// Gets the user-facing action label.
    /// </summary>
    public required string Label { get; init; }

    /// <summary>
    /// Gets the action kind.
    /// </summary>
    public required PrimaryActionKind Kind { get; init; }

    /// <summary>
    /// Gets the optional action payload.
    /// </summary>
    public IReadOnlyDictionary<string, string> Payload { get; init; } =
        new Dictionary<string, string>(StringComparer.Ordinal);
}
