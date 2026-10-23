// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Immutable runtime request to set every override of one part of a node's geometry, or of the geometry component itself.
/// </summary>
/// <remarks>
/// The request carries the part's whole override state: a property of the part with no entry takes the geometry's asset
/// value, so an empty list clears the part. Each override names a property of the addressed part kind.
/// </remarks>
/// <param name="NodeId">The node whose renderable the overrides change.</param>
/// <param name="GeometryPath">The engine path of the geometry the overrides were resolved against.</param>
/// <param name="Part">The surface addressed, or <see langword="null"/> for the geometry component's own properties.</param>
/// <param name="Overrides">The part's overrides, at most one per property.</param>
/// <param name="Intent">Whether this is a new observed edit or a retained authored override.</param>
public sealed record RuntimeSetOverrides(
    NodeId NodeId,
    string GeometryPath,
    RuntimeOverridePart? Part,
    ImmutableArray<RuntimeOverride> Overrides,
    OverrideIntent Intent) : RuntimeWorldCommand;
