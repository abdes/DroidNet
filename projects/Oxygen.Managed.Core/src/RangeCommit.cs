// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>What an inspector edit does with a finite value outside a property's range.</summary>
/// <remarks>Loading, cooking and the runtime always reject such a value: saved or imported source is never repaired.</remarks>
public enum RangeCommit
{
    /// <summary>The edit is rejected.</summary>
    Reject = 0,

    /// <summary>The edit commits the nearest value inside the range.</summary>
    Clamp = 1,
}
