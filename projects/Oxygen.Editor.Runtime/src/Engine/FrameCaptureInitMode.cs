// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed frame capture init mode options, converted at the native session boundary.</summary>
public enum FrameCaptureInitMode
{
    /// <summary>Disabled.</summary>
    Disabled = 0,

    /// <summary>Attached Only.</summary>
    AttachedOnly = 1,

    /// <summary>Search.</summary>
    Search = 2,

    /// <summary>Explicit Path.</summary>
    ExplicitPath = 3,
}
