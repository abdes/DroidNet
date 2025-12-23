// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents import progress updates.
/// </summary>
/// <param name="Stage">A stable stage name (for example <c>Probe</c>, <c>Import</c>, <c>Build</c>).</param>
/// <param name="CurrentItem">An optional human-readable identifier for the current item.</param>
/// <param name="Completed">Number of completed items.</param>
/// <param name="Total">Total number of items.</param>
public sealed record ImportProgress(
    string Stage,
    string? CurrentItem,
    int Completed,
    int Total);
