// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed physics backend options, converted at the native session boundary.</summary>
public enum PhysicsBackend
{
    /// <summary>None.</summary>
    None = 0,

    /// <summary>Jolt.</summary>
    Jolt = 1,

    /// <summary>Phys X.</summary>
    PhysX = 2,
}
