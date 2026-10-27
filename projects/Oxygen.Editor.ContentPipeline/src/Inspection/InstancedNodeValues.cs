// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// The effective values of a node an instance of a source brings in, read from the cooked scene descriptors of the
/// mounted content with every change the source and the scenes it instances make applied (scene instances 6.3, 8.5).
/// </summary>
/// <param name="Name">The node's name in the source.</param>
/// <param name="Geometry">
/// The geometry the node renders, or <see langword="null"/> when it renders none or no mounted content provides it.
/// </param>
/// <param name="Values">
/// Every property the node holds, each of a type <see cref="SceneSchema.Changeable"/> gives, then the geometry values it
/// replaces.
/// </param>
public sealed record InstancedNodeValues(string Name, AssetPath? Geometry, ImmutableArray<InstancedValue> Values);
