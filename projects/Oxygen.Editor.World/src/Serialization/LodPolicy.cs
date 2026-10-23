// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
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

    /// <summary>The widest hysteresis band of a distance policy.</summary>
    public const float MaxHysteresisRatio = 0.99f;

    /// <summary>
    /// Gets the rule the policy breaks, or <see langword="null"/> when the engine accepts it: a fixed LOD below
    /// <see cref="MaxLodCount"/>; at most <see cref="MaxBoundaryCount"/> finite, nonnegative boundaries; distances that
    /// never decrease with a hysteresis ratio in <c>[0, </c><see cref="MaxHysteresisRatio"/><c>]</c>; and as many exit
    /// as enter screen-space errors, each list never increasing.
    /// </summary>
    /// <returns>The broken rule, or <see langword="null"/>.</returns>
    public abstract string? FindProblem();

    // A boundary list: finite, nonnegative and ordered toward coarser LODs.
    private protected static string? FindListProblem(string name, ImmutableArray<float> values, bool rising)
    {
        if (values.IsDefault || values.Length > MaxBoundaryCount)
        {
            return string.Create(CultureInfo.InvariantCulture, $"{name} must hold at most {MaxBoundaryCount} boundaries.");
        }

        for (var i = 0; i < values.Length; i++)
        {
            if (!float.IsFinite(values[i]) || values[i] < 0f)
            {
                return $"{name} must be finite and not negative.";
            }

            if (i > 0 && (rising ? values[i] < values[i - 1] : values[i] > values[i - 1]))
            {
                return $"{name} must not {(rising ? "decrease" : "increase")}.";
            }
        }

        return null;
    }

    /// <summary>Always shows one LOD, clamped to the geometry's LOD count.</summary>
    /// <param name="LodIndex">The LOD shown.</param>
    public sealed record Fixed([property: JsonPropertyName("lod_index")] int LodIndex) : LodPolicy
    {
        /// <inheritdoc/>
        public override string? FindProblem()
            => this.LodIndex is >= 0 and < MaxLodCount ? null : string.Create(CultureInfo.InvariantCulture, $"The fixed LOD must be below {MaxLodCount}.");
    }

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

        /// <inheritdoc/>
        public override string? FindProblem()
            => FindListProblem("Distance thresholds", this.Thresholds, rising: true)
                ?? (this.HysteresisRatio is >= 0f and <= MaxHysteresisRatio
                    ? null
                    : string.Create(CultureInfo.InvariantCulture, $"The hysteresis ratio must lie in [0, {MaxHysteresisRatio}]."));
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

        /// <inheritdoc/>
        public override string? FindProblem()
            => FindListProblem("Enter-finer errors", this.EnterFinerSse, rising: false)
                ?? FindListProblem("Exit-coarser errors", this.ExitCoarserSse, rising: false)
                ?? (this.EnterFinerSse.Length == this.ExitCoarserSse.Length
                    ? null
                    : "A screen-space error policy needs as many exit as enter errors.");
    }
}
