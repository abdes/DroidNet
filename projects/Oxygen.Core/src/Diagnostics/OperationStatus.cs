// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Outcome status for a user-triggered operation.
/// </summary>
public enum OperationStatus
{
    /// <summary>
    /// Operation completed without warnings or errors.
    /// </summary>
    Succeeded,

    /// <summary>
    /// Operation completed with warning diagnostics.
    /// </summary>
    SucceededWithWarnings,

    /// <summary>
    /// Operation primary goal completed, but one or more sub-steps failed.
    /// </summary>
    PartiallySucceeded,

    /// <summary>
    /// Operation primary goal did not complete.
    /// </summary>
    Failed,

    /// <summary>
    /// Operation was cancelled before completion.
    /// </summary>
    Cancelled,
}
