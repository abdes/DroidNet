// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Field ids for <see cref="EngineComponentId.Node"/> rendering flags.
/// Mirrors <c>oxygen::interop::module::NodeField</c>.
/// </summary>
public enum NodeField
{
    /// <summary>Scene visibility.</summary>
    Visible = 0,

    /// <summary>Geometry casts shadows.</summary>
    CastsShadows = 1,

    /// <summary>Geometry receives shadows.</summary>
    ReceivesShadows = 2,
}
