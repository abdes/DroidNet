// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Re-points a scene's asset references through an identity mapping, in place: geometry, material slots (component
/// and targeted), slot targets, scene references and the environment's metering mask. Used when an open scene follows
/// a relocation, and when undo, redo or paste restores references captured before a relocation.
/// </summary>
internal static class SceneReferenceRetargeting
{
    /// <summary>Re-points one node's own components.</summary>
    /// <param name="node">The node.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>Whether any reference changed.</returns>
    public static bool RetargetNode(SceneNode node, Func<Uri, Uri?> map)
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

        return changed;
    }

    /// <summary>Re-points a node and every node below it.</summary>
    /// <param name="node">The subtree root.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The nodes that changed.</returns>
    public static IReadOnlyList<SceneNode> RetargetSubtree(SceneNode node, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(node);
        return [.. new[] { node }.Concat(node.Descendants()).Where(item => RetargetNode(item, map))];
    }

    /// <summary>Re-points scene references.</summary>
    /// <param name="references">The references.</param>
    /// <param name="map">Returns the new reference, keeping its form, or null when it does not move.</param>
    /// <returns>The re-pointed references, or null when nothing changed.</returns>
    public static SceneReferencesData? Retarget(SceneReferencesData references, Func<string, string?> map)
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
            InputMappingContexts = Uris(references.InputMappingContexts),
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

    /// <summary>Re-points the environment's metering mask.</summary>
    /// <param name="environment">The environment.</param>
    /// <param name="map">Returns the new URI, or null when the reference does not move.</param>
    /// <returns>The re-pointed environment, or null when nothing changed.</returns>
    public static SceneEnvironmentData? Retarget(SceneEnvironmentData environment, Func<Uri, Uri?> map)
    {
        ArgumentNullException.ThrowIfNull(environment);
        ArgumentNullException.ThrowIfNull(map);
        return environment.PostProcess.AutoExposureMeteringMask is { } mask && map(mask) is { } mapped
            ? environment with { PostProcess = environment.PostProcess with { AutoExposureMeteringMask = mapped } }
            : null;
    }
}
