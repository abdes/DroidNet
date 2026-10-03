// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Browsing scope, independent of authored property ownership.</summary>
public enum InspectorPropertyScope
{
    /// <summary>All existing scene fields.</summary>
    All,

    /// <summary>Atmosphere, source assignments and background.</summary>
    Environment,

    /// <summary>Exposure, tone mapping, grading and bloom.</summary>
    PostProcessing,
}
