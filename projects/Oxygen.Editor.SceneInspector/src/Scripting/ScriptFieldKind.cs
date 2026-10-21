// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>
/// The editor a script parameter gets, chosen from its type and hints.
/// </summary>
public enum ScriptFieldKind
{
    /// <summary>A toggle for a boolean.</summary>
    Toggle,

    /// <summary>A number box for an integer or a float, with range, step and unit.</summary>
    Number,

    /// <summary>A list of the declared options for a string.</summary>
    Choice,

    /// <summary>A text box for a string.</summary>
    Text,

    /// <summary>One number box per component.</summary>
    Vector,

    /// <summary>Color components with a color picker.</summary>
    Color,

    /// <summary>An asset picker filtered by the declared kind.</summary>
    Asset,

    /// <summary>A scene node picker.</summary>
    Node,
}
