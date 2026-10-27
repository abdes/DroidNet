// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// The LOD policies the Level of detail editor shows and writes: one boundary per transition between two of the geometry's LODs.
/// </summary>
/// <remarks>
/// The engine reads a policy's first <c>LOD count - 1</c> boundaries and ignores the others, so a policy is shown and
/// written fitted to the geometry: a missing boundary repeats the last one, which keeps the order the engine requires.
/// </remarks>
public static class LodPolicyShapes
{
    /// <summary>The kind index of <see cref="LodPolicy.Fixed"/>.</summary>
    public const int FixedKind = 0;

    /// <summary>The kind index of <see cref="LodPolicy.Distance"/>.</summary>
    public const int DistanceKind = 1;

    /// <summary>The kind index of <see cref="LodPolicy.ScreenSpaceError"/>.</summary>
    public const int ScreenSpaceErrorKind = 2;

    // The engine's default band around a distance boundary.
    private const float DefaultHysteresisRatio = 0.1f;

    // The cooker's default for an imported geometry with several LODs (GeometryDefaults MakeImportedLodPolicy): the
    // first boundary enters its finer LOD at a projected radius of 256 pixels, each next one at half of that, and each
    // enters its coarser LOD at 0.8 of its finer error.
    private const float FinestEnterPixels = 256f;
    private const float ExitRatio = 0.8f;

    // The distance boundaries, in bounding-sphere radii, that match those errors in a 1080p view with a 60 degree
    // vertical field of view: 4, 8, 16 and so on.
    private const float FinestDistanceRadii = 4f;

    /// <summary>Gets the names of the policy kinds, in kind-index order.</summary>
    public static IReadOnlyList<string> KindNames { get; } = ["Fixed", "Distance", "Screen-space error"];

    /// <summary>Gets the kind index of a policy.</summary>
    /// <param name="policy">The policy.</param>
    /// <returns>Its index in <see cref="KindNames"/>.</returns>
    public static int KindOf(LodPolicy policy) => policy switch
    {
        LodPolicy.Fixed => FixedKind,
        LodPolicy.Distance => DistanceKind,
        LodPolicy.ScreenSpaceError => ScreenSpaceErrorKind,
        _ => throw new ArgumentOutOfRangeException(nameof(policy), policy, "The policy has no kind the editor lists."),
    };

    /// <summary>Gets the name of a policy's kind.</summary>
    /// <param name="policy">The policy.</param>
    /// <returns>The name, as the editor lists it.</returns>
    public static string NameOf(LodPolicy policy) => KindNames[KindOf(policy)];

    /// <summary>
    /// Creates a policy of a kind for a geometry: the asset's own when it is of that kind, otherwise the cooker's
    /// default screen-space errors for its LODs, or the distances that match them.
    /// </summary>
    /// <param name="kind">The kind index.</param>
    /// <param name="asset">The asset's policy.</param>
    /// <param name="lodCount">The geometry's number of LODs.</param>
    /// <returns>A policy the engine accepts.</returns>
    public static LodPolicy Create(int kind, LodPolicy asset, int lodCount)
    {
        if (KindOf(asset) == kind)
        {
            return Fit(asset, lodCount);
        }

        return kind switch
        {
            FixedKind => new LodPolicy.Fixed(0),
            DistanceKind => new LodPolicy.Distance(DefaultDistances(lodCount), DefaultHysteresisRatio),
            ScreenSpaceErrorKind => new LodPolicy.ScreenSpaceError(DefaultEnterErrors(lodCount), DefaultExitErrors(lodCount)),
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "The kind has no policy."),
        };
    }

    /// <summary>Fits a policy's boundaries to a geometry's LODs, one per transition.</summary>
    /// <param name="policy">The policy.</param>
    /// <param name="lodCount">The geometry's number of LODs.</param>
    /// <returns>The policy as the engine applies it to the geometry.</returns>
    public static LodPolicy Fit(LodPolicy policy, int lodCount) => policy switch
    {
        LodPolicy.Distance distance => distance with { Thresholds = Fit(distance.Thresholds, DefaultDistances(lodCount)) },
        LodPolicy.ScreenSpaceError error => new LodPolicy.ScreenSpaceError(
            Fit(error.EnterFinerSse, DefaultEnterErrors(lodCount)),
            Fit(error.ExitCoarserSse, DefaultExitErrors(lodCount))),
        _ => policy,
    };

    private static ImmutableArray<float> Fit(ImmutableArray<float> values, ImmutableArray<float> defaults)
        => [.. defaults.Select((fallback, index) => index < values.Length ? values[index] : values.IsEmpty ? fallback : values[^1])];

    private static ImmutableArray<float> DefaultDistances(int lodCount)
        => [.. Enumerable.Range(0, Transitions(lodCount)).Select(static boundary => FinestDistanceRadii * MathF.Pow(2f, boundary))];

    private static ImmutableArray<float> DefaultEnterErrors(int lodCount)
        => [.. Enumerable.Range(0, Transitions(lodCount)).Select(static boundary => FinestEnterPixels / MathF.Pow(2f, boundary))];

    private static ImmutableArray<float> DefaultExitErrors(int lodCount)
        => [.. DefaultEnterErrors(lodCount).Select(static error => error * ExitRatio)];

    private static int Transitions(int lodCount) => Math.Clamp(lodCount, 1, LodPolicy.MaxLodCount) - 1;
}
