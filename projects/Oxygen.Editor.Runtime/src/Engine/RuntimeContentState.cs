// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The native acknowledgement state, independently of saved-source or cooked-file freshness.</summary>
public enum RuntimeContentState
{
    /// <summary>No running native session can use content.</summary>
    Unavailable,

    /// <summary>The engine is running without acknowledged project roots.</summary>
    Unmounted,

    /// <summary>The engine is replacing roots or waiting to resume the preview.</summary>
    Updating,

    /// <summary>Root replacement and preview resume were acknowledged by the native session.</summary>
    Mounted,

    /// <summary>A native content operation failed; root availability is unproven.</summary>
    Failed,
}
