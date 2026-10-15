// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets.Runtime;

/// <summary>The state of the preview's cooked content.</summary>
public enum AssetRuntimeContentState
{
    /// <summary>No preview run can show content: it is not running, or its content failed.</summary>
    Unavailable,

    /// <summary>The preview runs with no cooked content mounted.</summary>
    Unmounted,

    /// <summary>The preview is replacing its mounted content.</summary>
    Updating,

    /// <summary>The preview has mounted the reported roots.</summary>
    Mounted,
}
