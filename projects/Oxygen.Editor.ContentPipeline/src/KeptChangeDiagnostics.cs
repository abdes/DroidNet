// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// The cook's warnings about kept changes, changes the cooker leaves out because their node or surface is gone (scene
/// instances 8.10): each names its node, and Go To Property selects it and shows its changes.
/// </summary>
public static class KeptChangeDiagnostics
{
    /// <summary>The property path Go To Property focuses: the caution box of the Scene Instance section.</summary>
    public const string PropertyPath = "SceneInstance.KeptChanges";

    /// <summary>An instance's change names no node of its source, or a property that node does not have.</summary>
    public const string InstanceOverrideUnresolved = "scene.descriptor.instance_override_unresolved";

    /// <summary>An override or change targets a surface the geometry does not declare.</summary>
    public const string SurfaceInvalid = "scene.descriptor.surface_invalid";

    /// <summary>An instance's script parameter change names no node of its source, or a slot that node does not have.</summary>
    public const string ScriptInstanceOverrideUnresolved = "script.sidecar.instance_override_unresolved";

    /// <summary>Gets whether a cooker code is a warning about a kept change.</summary>
    /// <param name="code">The code.</param>
    /// <returns><see langword="true"/> for the three kept-change codes.</returns>
    public static bool IsKeptChange(string? code)
        => string.Equals(code, InstanceOverrideUnresolved, StringComparison.Ordinal)
            || string.Equals(code, SurfaceInvalid, StringComparison.Ordinal)
            || string.Equals(code, ScriptInstanceOverrideUnresolved, StringComparison.Ordinal);

    /// <summary>Creates the diagnostic of a kept change, naming its node, with Go To Property.</summary>
    /// <param name="issue">The cooker's diagnostic.</param>
    /// <param name="scene">The scene being cooked.</param>
    /// <param name="descriptor">The scene's descriptor projection.</param>
    /// <param name="node">The node the change is stored on: the instance root, or an owned node with a lost surface.</param>
    /// <param name="change">The instanced node's change, or <see langword="null"/> for an owned node's override.</param>
    /// <returns>The diagnostic.</returns>
    public static DiagnosticRecord Create(DiagnosticRecord issue, ContentCookInput scene, SceneDescriptorGenerationResult descriptor, DescriptorNodeSite node, DescriptorInstanceChange? change)
    {
        ArgumentNullException.ThrowIfNull(issue);
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentNullException.ThrowIfNull(node);

        // A lost surface is listed with its node's surfaces, in the Geometry section; a lost node, in the instance root's
        // kept changes.
        var lostSurface = string.Equals(issue.Code, SurfaceInvalid, StringComparison.Ordinal);
        var target = change is null ? NodeReference.To(node.NodeId) : new NodeReference(node.NodeId, change.Path);
        var subject = change is null
            ? $"'{node.NodeName}'"
            : $"Kept change of '{change.Names ?? change.Path.ToString()}' in the instance under '{node.NodeName}'";
        return issue with
        {
            Message = $"{subject}: {issue.Message}",
            AffectedPath = scene.SourceAbsolutePath.FullPath,
            AffectedVirtualPath = scene.AssetUri.VirtualPath,
            AffectedEntity = new AffectedScope
            {
                SceneId = descriptor.SceneId,
                SceneName = descriptor.SceneName,
                NodeId = node.NodeId,
                NodeName = node.NodeName,
                ComponentName = change is null || lostSurface ? "Geometry" : "Scene Instance",
                AssetVirtualPath = scene.AssetUri.VirtualPath,
            },
            SuggestedAction = new PrimaryAction
            {
                ActionId = "Cook.GoToProperty",
                Label = change is null || lostSurface ? "Select node" : "Go to kept changes",
                Kind = PrimaryActionKind.Custom,
                Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["AssetUri"] = scene.AssetUri.ToUriString(),
                    ["NodeId"] = node.NodeId.ToString(),
                    ["NodeReference"] = target.ToString(),
                    ["PropertyPath"] = change is null || lostSurface ? string.Empty : PropertyPath,
                },
            },
        };
    }
}
