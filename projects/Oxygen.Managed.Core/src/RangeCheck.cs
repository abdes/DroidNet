// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>The result of checking a value against a property's range.</summary>
public enum RangeCheck
{
    /// <summary>The value is valid.</summary>
    InRange = 0,

    /// <summary>The value is NaN or infinite.</summary>
    NotFinite = 1,

    /// <summary>The value is finite but outside the range, or zero where zero is excluded.</summary>
    OutOfRange = 2,
}
