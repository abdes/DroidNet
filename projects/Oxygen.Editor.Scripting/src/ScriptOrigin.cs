// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Where what is known of a script comes from.
/// </summary>
public enum ScriptOrigin
{
    /// <summary>The analysis of its source in the project.</summary>
    Source,

    /// <summary>The cooked script of a mounted library, which has no source in the project.</summary>
    Library,
}
