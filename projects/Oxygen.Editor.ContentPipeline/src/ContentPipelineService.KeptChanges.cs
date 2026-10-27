// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.RegularExpressions;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <content>Maps the cook's warnings about kept changes to their nodes (scene instances 8.10).</content>
public sealed partial class ContentPipelineService
{
    /// <summary>Maps a cooker warning about a kept change to the node that stores it.</summary>
    /// <param name="scope">The cook scope.</param>
    /// <param name="descriptors">The cooked scenes' projections.</param>
    /// <param name="issue">The diagnostic, one of <see cref="KeptChangeDiagnostics.IsKeptChange"/>.</param>
    /// <returns>The diagnostic naming its node, with Go To Property, or unchanged when its location names no node.</returns>
    internal static DiagnosticRecord MapKeptChangeDiagnostic(ContentCookScope scope, IReadOnlyList<SceneDescriptorGenerationResult> descriptors, DiagnosticRecord issue)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(descriptors);
        ArgumentNullException.ThrowIfNull(issue);
        if (issue.TechnicalMessage is not { } location)
        {
            return issue;
        }

        // The cooker reports against the scene job's descriptor; a manifest cooks each scene from its own descriptor.
        var descriptor = descriptors.FirstOrDefault(candidate => issue.AffectedPath is { } affected && FileSystemPath.From(candidate.DescriptorPath) == FileSystemPath.From(affected))
            ?? (descriptors.Count == 1 ? descriptors[0] : null);
        if (descriptor is null || scope.Inputs.FirstOrDefault(input => input.AssetUri == descriptor.SceneAssetUri) is not { } scene)
        {
            return issue;
        }

        if (string.Equals(issue.Code, KeptChangeDiagnostics.ScriptInstanceOverrideUnresolved, StringComparison.Ordinal))
        {
            return InstanceBindingLocation().Match(location) is { Success: true } binding
                && Index(binding, "binding") is var index && index < descriptor.ScriptInstanceBindingSites.Count
                && descriptor.ScriptInstanceBindingSites[index] is var site
                && descriptor.Nodes.FirstOrDefault(node => node.NodeId == site.NodeId) is { } root
                    ? KeptChangeDiagnostics.Create(issue, scene, descriptor, root, new DescriptorInstanceChange(site.Path, site.InstancedNames))
                    : issue;
        }

        if (DescriptorLocation().Match(location) is not { Success: true } match)
        {
            return issue;
        }

        if (match.Groups["renderable"].Success)
        {
            var renderable = Index(match, "renderable");
            return renderable < descriptor.RenderableNodes.Count && descriptor.RenderableNodes[renderable] < descriptor.Nodes.Count
                ? KeptChangeDiagnostics.Create(issue, scene, descriptor, descriptor.Nodes[descriptor.RenderableNodes[renderable]], change: null)
                : issue;
        }

        var nodeIndex = Index(match, "node");
        if (nodeIndex >= descriptor.Nodes.Count)
        {
            return issue;
        }

        var node = descriptor.Nodes[nodeIndex];
        var change = match.Groups["change"].Success && Index(match, "change") is var changeIndex && changeIndex < node.InstanceChanges.Count
            ? node.InstanceChanges[changeIndex]
            : null;
        return KeptChangeDiagnostics.Create(issue, scene, descriptor, node, change);
    }

    private static int Index(Match match, string group) => int.Parse(match.Groups[group].Value, CultureInfo.InvariantCulture);

    [GeneratedRegex(@"^(?:nodes\[(?<node>\d+)\](?:\.instance\.overrides\[(?<change>\d+)\])?|renderables\[(?<renderable>\d+)\])", RegexOptions.CultureInvariant, matchTimeoutMilliseconds: 1000)]
    private static partial Regex DescriptorLocation();

    [GeneratedRegex(@"^instance_bindings\[(?<binding>\d+)\]", RegexOptions.CultureInvariant, matchTimeoutMilliseconds: 1000)]
    private static partial Regex InstanceBindingLocation();
}
