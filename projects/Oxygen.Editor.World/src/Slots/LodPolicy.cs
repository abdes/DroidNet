// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
///     Base class for Level of Detail (LOD) selection policies.
/// </summary>
/// <remarks>
///     LOD policies determine which LOD level to render based on various criteria. The engine
///     evaluates these policies at runtime to optimize rendering performance.
///     <para>
///     Because LOD policies are plain value objects (POCOs) with no observability, side effects, or
///     lifecycle concerns, they’re treated as domain value objects and serialized directly — they
///     don’t need separate DTO wrappers.</para>
/// </remarks>
[JsonPolymorphic(TypeDiscriminatorPropertyName = "$type")]
[JsonDerivedType(typeof(FixedLodPolicy), "Fixed")]
[JsonDerivedType(typeof(DistanceLodPolicy), "Distance")]
[JsonDerivedType(typeof(ScreenSpaceErrorLodPolicy), "ScreenSpaceError")]
public abstract class LodPolicy
{
}
