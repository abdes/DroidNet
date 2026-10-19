// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <summary>
/// What a property that references an asset accepts: the assets of one kind, from the engine content, mounted paks
/// and the project's content, and whether they must already have cooked output.
/// </summary>
/// <remarks>
/// <para>
/// The kinds a property can reference are the ones the pipeline cooks into an asset (<see cref="AssetFileTypes"/>), so
/// a kind added to the file types is accepted by its properties without a rule of its own; the rule of any other kind
/// accepts nothing, such as a script parameter of an engine type the editor does not list. Engine-internal assets
/// (<see cref="EngineAssets.Internal"/>) are never offered.
/// </para>
/// <para>
/// A rule never accepts an asset whose file is missing or broken. <see cref="Apply"/> keeps a property's current
/// assignment even then, so the user sees what it points to; it never lists an asset of another kind, or an
/// engine-internal one.
/// </para>
/// </remarks>
public sealed class AssetRule
{
    private static readonly IReadOnlyDictionary<AssetKind, (AssetRule Any, AssetRule Cooked)> Rules
        = AssetFileTypes.Suffixes(AssetFileCategory.Cooked)
            .Select(static suffix => AssetKinds.FromFileName(suffix))
            .Where(static kind => kind != AssetKind.Unknown)
            .Distinct()
            .ToDictionary(
                static kind => kind,
                static kind => (new AssetRule(kind, cooked: false, referenceable: true), new AssetRule(kind, cooked: true, referenceable: true)));

    private readonly bool referenceable;

    private AssetRule(AssetKind kind, bool cooked, bool referenceable)
    {
        this.Kind = kind;
        this.Cooked = cooked;
        this.referenceable = referenceable;
    }

    /// <summary>Gets the kind the property accepts.</summary>
    public AssetKind Kind { get; }

    /// <summary>
    /// Gets a value indicating whether the property references the cooked asset the engine loads, so that only assets
    /// with cooked output are accepted.
    /// </summary>
    public bool Cooked { get; }

    /// <summary>Gets the rule of a property that references assets of a kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The rule.</returns>
    public static AssetRule For(AssetKind kind) => Find(kind, cooked: false);

    /// <summary>
    /// Gets the rule of a property that references the cooked asset the engine loads, such as a scene reference or a
    /// script parameter: assets of a kind that have cooked output.
    /// </summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The rule.</returns>
    public static AssetRule ForCooked(AssetKind kind) => Find(kind, cooked: true);

    /// <summary>Checks whether the property accepts an asset.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns><see langword="true"/> when the asset may be assigned.</returns>
    public bool Accepts(AssetEntry asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return this.IsOfKind(asset)
            && asset.IsSelectable
            && (!this.Cooked || asset.CookedMetadata is not null || asset.CookStatus?.HasAvailableOutput == true);
    }

    /// <summary>
    /// Gives the assets the property accepts, by name, and keeps its current assignments: an assigned asset whose file
    /// is missing or broken is listed as it is, and one no catalog knows is listed as missing.
    /// </summary>
    /// <param name="assets">The feed's assets.</param>
    /// <param name="keeping">The property's current assignments; null entries are ignored.</param>
    /// <returns>The accepted and kept assets, ordered by name and then path.</returns>
    public IReadOnlyList<AssetEntry> Apply(IReadOnlyList<AssetEntry> assets, params IEnumerable<AssetPath?> keeping)
    {
        ArgumentNullException.ThrowIfNull(assets);
        ArgumentNullException.ThrowIfNull(keeping);
        var accepted = assets.Where(this.Accepts).ToList();
        foreach (var identity in keeping.OfType<AssetPath>())
        {
            if (accepted.Any(asset => asset.Identity == identity))
            {
                continue;
            }

            var assigned = assets.FirstOrDefault(asset => asset.Identity == identity);
            if (assigned is null)
            {
                accepted.Add(AssetEntry.Missing(identity));
            }
            else if (this.IsOfKind(assigned))
            {
                accepted.Add(assigned);
            }
        }

        return [.. accepted
            .DistinctBy(static asset => asset.Identity)
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(static asset => asset.DisplayPath, StringComparer.OrdinalIgnoreCase)];
    }

    private static AssetRule Find(AssetKind kind, bool cooked)
        => Rules.TryGetValue(kind, out var rules)
            ? cooked ? rules.Cooked : rules.Any
            : new AssetRule(kind, cooked, referenceable: false);

    // Engine-internal assets, such as the transform gizmo's arrow, are the engine's own and are never offered.
    private bool IsOfKind(AssetEntry asset) => this.referenceable && asset.Kind == this.Kind && !asset.Identity.IsSameOrInside(EngineAssets.Internal);
}
