// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

/// <summary>Distinguishes editor composition changes from values on already selected components.</summary>
internal enum InspectorSelectionChange
{
    /// <summary>The applicable component composition changed.</summary>
    Structure,

    /// <summary>Values or material slots on selected components changed.</summary>
    Values,
}
