// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Components;

/// <summary>The resolved value of a node flag that has a source mode, and where it comes from.</summary>
/// <param name="Value">The resolved value.</param>
/// <param name="Source">
/// The node whose local mode gives the value: the node itself or the ancestor an Inherit chain reaches;
/// <see langword="null"/> when the chain passes the root and the value is the scene default.
/// </param>
public readonly record struct ResolvedNodeFlag(bool Value, SceneNode? Source);
