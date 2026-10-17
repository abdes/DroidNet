// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The values one property edit writes to one target.</summary>
/// <param name="Node">The node, or <see langword="null"/> for the scene's own properties.</param>
/// <param name="Edit">The values.</param>
public sealed record PropertyTargetEdit(NodeId? Node, PropertyEdit Edit);
