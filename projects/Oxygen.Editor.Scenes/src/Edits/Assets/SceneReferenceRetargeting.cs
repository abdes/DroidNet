// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>
/// Re-points a scene's asset references through an identity mapping, in place: geometry, material slots (component
/// and targeted), slot targets, script slots, scene references, starting input contexts and the environment's metering mask. Used when an open scene follows
/// a relocation, and when undo, redo or paste restores references captured before a relocation.
/// </summary>
internal static class SceneReferenceRetargeting
{
    /// <summary>Re-points one node's own components.</summary>
    /// <param name="node">The node.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>Whether any reference changed.</returns>
    internal static bool RetargetNode(SceneNode node, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(map);
        var changed = false;
        foreach (var geometry in node.Components.OfType<GeometryComponent>())
        {
            if (geometry.Geometry?.Uri is { } geometryUri && map(geometryUri) is { } mapped)
            {
                geometry.Geometry = new AssetReference<GeometryAsset>(mapped);
                changed = true;
            }

            foreach (var slot in geometry.OverrideSlots.Concat(geometry.TargetedOverrides.SelectMany(static target => target.OverrideSlots)).OfType<MaterialsSlot>())
            {
                if (map(slot.Material.Uri) is { } material)
                {
                    slot.Material = new AssetReference<MaterialAsset>(material);
                    changed = true;
                }

                if (map(slot.Target.GeometryUri) is { } target)
                {
                    slot.Target = slot.Target with { GeometryUri = target };
                    changed = true;
                }
            }
        }

        foreach (var scripting in node.Components.OfType<ScriptingComponent>())
        {
            if (Retarget(scripting.Slots, map) is { } slots)
            {
                scripting.Slots = slots;
                changed = true;
            }
        }

        return changed;
    }

    /// <summary>Re-points script slots: their scripts and their asset parameter values.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The re-pointed slots, or null when nothing changed.</returns>
    internal static ImmutableArray<ScriptSlot>? Retarget(ImmutableArray<ScriptSlot> slots, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var changed = false;
        var result = slots.Select(slot =>
        {
            var updated = slot.Script is { } script && map(script) is { } moved ? slot with { Script = moved } : slot;
            foreach (var (name, value) in slot.Overrides)
            {
                if (value.AssetUri is { } asset && map(asset) is { } target)
                {
                    updated = updated.WithOverride(name, ScriptValue.FromAsset(target));
                }
            }

            changed |= !ReferenceEquals(updated, slot);
            return updated;
        }).ToImmutableArray();
        return changed ? result : null;
    }

    /// <summary>Re-points a node and every node below it.</summary>
    /// <param name="node">The subtree root.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The nodes that changed.</returns>
    internal static IReadOnlyList<SceneNode> RetargetSubtree(SceneNode node, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(node);
        return [.. new[] { node }.Concat(node.Descendants()).Where(item => RetargetNode(item, map))];
    }

    /// <summary>Re-points scene references.</summary>
    /// <param name="references">The references.</param>
    /// <param name="map">Returns the new reference, keeping its form, or null when it does not move.</param>
    /// <returns>The re-pointed references, or null when nothing changed.</returns>
    internal static SceneReferencesData? Retarget(SceneReferencesData references, Func<string, string?> map)
    {
        ArgumentNullException.ThrowIfNull(references);
        ArgumentNullException.ThrowIfNull(map);
        var changed = false;
        IList<Uri> Uris(IList<Uri> values) => [.. values.Select(value =>
        {
            if (map(value.ToString()) is not { } mapped)
            {
                return value;
            }

            changed = true;
            return new Uri(mapped);
        })];
        var result = references with
        {
            Scripts = Uris(references.Scripts),
            InputActions = Uris(references.InputActions),
            PhysicsSidecars = Uris(references.PhysicsSidecars),
            ExtraAssets = [.. references.ExtraAssets.Select(value =>
            {
                if (map(value) is not { } mapped)
                {
                    return value;
                }

                changed = true;
                return mapped;
            })],
        };
        return changed ? result : null;
    }

    /// <summary>Re-points the starting input contexts.</summary>
    /// <param name="contexts">The starting input contexts.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The re-pointed contexts, or null when nothing changed.</returns>
    internal static IReadOnlyList<StartingInputContextData>? Retarget(IReadOnlyList<StartingInputContextData> contexts, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(contexts);
        ArgumentNullException.ThrowIfNull(map);
        var changed = false;
        var retargeted = contexts.Select(entry =>
        {
            if (entry?.Context is not { } context || map(context) is not { } moved)
            {
                return entry!;
            }

            changed = true;
            return entry with { Context = moved };
        }).ToList();
        return changed ? retargeted : null;
    }

    /// <summary>Re-points the environment's metering mask and sky cubemaps.</summary>
    /// <param name="environment">The environment.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The re-pointed environment, or null when nothing changed.</returns>
    internal static SceneEnvironmentData? Retarget(SceneEnvironmentData environment, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(environment);
        ArgumentNullException.ThrowIfNull(map);
        var result = environment;
        if (Moved(environment.PostProcess.AutoExposureMeteringMask, map) is { } mask)
        {
            result = result with { PostProcess = result.PostProcess with { AutoExposureMeteringMask = mask } };
        }

        if (Moved(environment.SkySphere.Cubemap, map) is { } backdrop)
        {
            result = result with { SkySphere = result.SkySphere with { Cubemap = backdrop } };
        }

        if (Moved(environment.SkyLight.Cubemap, map) is { } light)
        {
            result = result with { SkyLight = result.SkyLight with { Cubemap = light } };
        }

        return ReferenceEquals(result, environment) ? null : result;

        static Uri? Moved(Uri? reference, Func<Uri, Uri?> map) => reference is null ? null : map(reference);
    }
}
