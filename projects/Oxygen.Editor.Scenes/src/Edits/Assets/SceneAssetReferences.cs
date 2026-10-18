// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>The assets a scene's nodes and environment reference.</summary>
public static class SceneAssetReferences
{
    /// <summary>Gets the asset references the runtime shows for a node: its geometry and material slots.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The geometry and the materials of its component and targeted slots.</returns>
    public static IEnumerable<Uri> Runtime(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return node.Components.OfType<GeometryComponent>().SelectMany(static geometry => new[] { geometry.Geometry }
            .Concat(geometry.OverrideSlots.Concat(geometry.TargetedOverrides.SelectMany(static target => target.OverrideSlots))
                .OfType<MaterialsSlot>().Select(static slot => slot.Material)));
    }

    /// <summary>Gets the asset references a node's script slots hold.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The scripts and asset parameter values.</returns>
    public static IEnumerable<Uri> Scripts(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return node.Components.OfType<ScriptingComponent>().SelectMany(static scripting => scripting.Slots)
            .SelectMany(static slot => new[] { slot.Script }.Concat(slot.Overrides.Values.Select(static value => value.AssetUri)))
            .OfType<Uri>();
    }

    /// <summary>Gets the asset references an environment holds.</summary>
    /// <param name="environment">The environment.</param>
    /// <returns>The metering mask and sky cubemaps that are set.</returns>
    public static IEnumerable<Uri> Environment(SceneEnvironmentData environment)
    {
        ArgumentNullException.ThrowIfNull(environment);
        return new[] { environment.PostProcess.AutoExposureMeteringMask, environment.SkySphere.Cubemap, environment.SkyLight.Cubemap }.OfType<Uri>();
    }
}
