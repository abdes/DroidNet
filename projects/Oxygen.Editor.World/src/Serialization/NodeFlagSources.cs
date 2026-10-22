// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>The source mode of a node's scene visibility: its parent's resolved value or a local one.</summary>
/// <remarks>The values are the wire values Interop applies; never renumber them.</remarks>
public enum VisibilitySource
{
    /// <summary>Takes the parent's resolved visibility; a root takes the scene default, Shown.</summary>
    Inherit = 0,

    /// <summary>Shown, whatever the parent's visibility.</summary>
    Shown = 1,

    /// <summary>Hidden, whatever the parent's visibility.</summary>
    Hidden = 2,
}

/// <summary>The source mode of a node's geometry shadow casting or receiving: its parent's resolved value or a local one.</summary>
/// <remarks>The values are the wire values Interop applies; never renumber them.</remarks>
public enum ShadowSource
{
    /// <summary>Takes the parent's resolved value; a root takes the scene default, On.</summary>
    Inherit = 0,

    /// <summary>On, whatever the parent's value.</summary>
    On = 1,

    /// <summary>Off, whatever the parent's value.</summary>
    Off = 2,
}
