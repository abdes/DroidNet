// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Optional filter used to query operation result snapshots.
/// </summary>
public sealed record OperationResultScopeFilter
{
    /// <summary>
    /// Gets the project identity to match.
    /// </summary>
    public Guid? ProjectId { get; init; }

    /// <summary>
    /// Gets the operation kind to match.
    /// </summary>
    public string? OperationKind { get; init; }

    /// <summary>
    /// Gets the failure domain to match.
    /// </summary>
    public FailureDomain? Domain { get; init; }
}
