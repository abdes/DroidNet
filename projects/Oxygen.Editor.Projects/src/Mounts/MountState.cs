// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>Whether a resolved mount has a location on this machine.</summary>
public enum MountState
{
    /// <summary>The mount is bound to a folder or file that exists.</summary>
    Bound = 0,

    /// <summary>The mount is a placeholder that nothing binds.</summary>
    Unbound = 1,

    /// <summary>The mount is bound to a folder or file that does not exist.</summary>
    Missing = 2,

    /// <summary>
    /// The mount is bound to a cooked library it cannot use. It stays in the table with its findings, contributes no
    /// assets, and is never added to a cook's, a run's or a preview's cooked roots.
    /// </summary>
    Invalid = 3,
}
