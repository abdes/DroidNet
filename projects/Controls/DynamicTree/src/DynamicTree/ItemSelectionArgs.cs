// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
/// Provides data for item selection operations in a dynamic tree, including the item,
/// the origin of the request, and modifier key states.
/// </summary>
/// <param name="Item">The tree item being selected or affected.</param>
/// <param name="Origin">The origin of the selection request (e.g., programmatic, pointer, keyboard).</param>
/// <param name="IsCtrlKeyDown">Indicates whether the Ctrl key was held during the selection.</param>
/// <param name="IsShiftKeyDown">Indicates whether the Shift key was held during the selection.</param>
public record ItemSelectionArgs(
    ITreeItem Item,
    RequestOrigin Origin = RequestOrigin.Programmatic,
    bool IsCtrlKeyDown = false,
    bool IsShiftKeyDown = false);
