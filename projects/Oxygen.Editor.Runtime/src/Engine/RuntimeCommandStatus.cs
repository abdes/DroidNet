// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The outcome at dispatch or native command completion, never presentation proof.</summary>
public enum RuntimeCommandStatus
{
    /// <summary>The boundary accepted the command.</summary>
    Accepted,

    /// <summary>The target or payload was rejected.</summary>
    Rejected,

    /// <summary>The required runtime capability is unavailable.</summary>
    Unavailable,

    /// <summary>The caller cancelled the operation.</summary>
    Cancelled,

    /// <summary>The operation failed.</summary>
    Failed,
}
