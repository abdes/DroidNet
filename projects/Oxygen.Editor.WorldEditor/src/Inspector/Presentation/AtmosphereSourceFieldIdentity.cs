// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Identifies a source-role presentation independently of its stored properties.</summary>
/// <param name="Role">The role whose existing light model supplies the editor.</param>
/// <param name="Key">The existing numeric/vector edit key, not a display caption.</param>
/// <param name="Properties">Canonical properties represented by this source editor.</param>
internal sealed record AtmosphereSourceFieldIdentity(
    AtmosphereLightSlot Role,
    string Key,
    ImmutableArray<PropertyId> Properties);
