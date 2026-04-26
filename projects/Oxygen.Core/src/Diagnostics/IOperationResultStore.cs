// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Current-session operation result store.
/// </summary>
public interface IOperationResultStore : IOperationResultPublisher
{
    /// <summary>
    /// Gets the latest operation result, if one has been published.
    /// </summary>
    public OperationResult? Latest { get; }

    /// <summary>
    /// Gets a snapshot of all retained operation results.
    /// </summary>
    /// <returns>A retained result snapshot.</returns>
    public IReadOnlyList<OperationResult> GetSnapshot();

    /// <summary>
    /// Gets a filtered snapshot of retained operation results.
    /// </summary>
    /// <param name="filter">The optional scope filter.</param>
    /// <returns>A retained result snapshot matching the filter.</returns>
    public IReadOnlyList<OperationResult> GetSnapshot(OperationResultScopeFilter? filter);
}
