// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// How a geometry selects its level of detail per view: the engine's LOD policy, the value of the scene schema's
/// <c>lod-policy</c> type.
/// </summary>
/// <remarks>
/// LOD 0 is the finest. A geometry has at most <see cref="MaxLodCount"/> LODs, so a policy has at most
/// <see cref="MaxBoundaryCount"/> boundaries, boundary <c>i</c> lying between LOD <c>i</c> and LOD <c>i + 1</c>. Its JSON
/// is the same in the saved scene, the descriptors and the inventory: <c>{ "type": "fixed", "lod_index": 0 }</c>,
/// <c>{ "type": "distance", "thresholds": [...], "hysteresis_ratio": 0.1 }</c> or
/// <c>{ "type": "screen_space_error", "enter_finer_sse": [...], "exit_coarser_sse": [...] }</c>.
/// </remarks>
[JsonPolymorphic(TypeDiscriminatorPropertyName = "type")]
[JsonDerivedType(typeof(Fixed), "fixed")]
[JsonDerivedType(typeof(Distance), "distance")]
[JsonDerivedType(typeof(ScreenSpaceError), "screen_space_error")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "tightly coupled records")]
public abstract record LodPolicy
{
    /// <summary>The most LODs a geometry has.</summary>
    public const int MaxLodCount = 8;

    /// <summary>The most boundaries a policy has, one fewer than <see cref="MaxLodCount"/>.</summary>
    public const int MaxBoundaryCount = MaxLodCount - 1;

    /// <summary>Always shows one LOD, clamped to the geometry's LOD count.</summary>
    /// <param name="LodIndex">The LOD shown.</param>
    public sealed record Fixed([property: JsonPropertyName("lod_index")] int LodIndex) : LodPolicy;

    /// <summary>Selects a LOD by normalized view distance.</summary>
    /// <param name="Thresholds">The distance of each boundary.</param>
    /// <param name="HysteresisRatio">The symmetric band around each boundary within which the current LOD is kept.</param>
    public sealed record Distance(
        [property: JsonPropertyName("thresholds")] ImmutableArray<float> Thresholds,
        [property: JsonPropertyName("hysteresis_ratio")] float HysteresisRatio) : LodPolicy
    {
        /// <inheritdoc/>
        public bool Equals(Distance? other)
            => other is not null
                && this.Thresholds.SequenceEqual(other.Thresholds)
                && this.HysteresisRatio.Equals(other.HysteresisRatio);

        /// <inheritdoc/>
        public override int GetHashCode() => HashCode.Combine(this.Thresholds.Length, this.HysteresisRatio);
    }

    /// <summary>Selects a LOD by screen-space error, with directional hysteresis.</summary>
    /// <param name="EnterFinerSse">The error of each boundary above which a finer LOD is entered.</param>
    /// <param name="ExitCoarserSse">The error of each boundary below which a coarser LOD is entered.</param>
    public sealed record ScreenSpaceError(
        [property: JsonPropertyName("enter_finer_sse")] ImmutableArray<float> EnterFinerSse,
        [property: JsonPropertyName("exit_coarser_sse")] ImmutableArray<float> ExitCoarserSse) : LodPolicy
    {
        /// <inheritdoc/>
        public bool Equals(ScreenSpaceError? other)
            => other is not null
                && this.EnterFinerSse.SequenceEqual(other.EnterFinerSse)
                && this.ExitCoarserSse.SequenceEqual(other.ExitCoarserSse);

        /// <inheritdoc/>
        public override int GetHashCode() => HashCode.Combine(this.EnterFinerSse.Length, this.ExitCoarserSse.Length);
    }
}
