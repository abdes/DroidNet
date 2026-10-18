// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.RegularExpressions;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Points the cooker's script sidecar diagnostics at the slot and parameter they are about.</summary>
public sealed partial class ContentPipelineService
{
    private const string ScriptSidecarCodePrefix = "script.sidecar.";

    /// <summary>Maps a cooker diagnostic about a sidecar binding to its node, slot and parameter.</summary>
    /// <param name="scope">The cook scope.</param>
    /// <param name="descriptors">The cooked scenes' projections.</param>
    /// <param name="issue">The diagnostic.</param>
    /// <returns>The diagnostic with Go To Property, or unchanged when it is not about a binding.</returns>
    internal static DiagnosticRecord MapScriptSidecarDiagnostic(ContentCookScope scope, IReadOnlyList<SceneDescriptorGenerationResult> descriptors, DiagnosticRecord issue)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(descriptors);
        ArgumentNullException.ThrowIfNull(issue);
        if (!issue.Code.StartsWith(ScriptSidecarCodePrefix, StringComparison.Ordinal)
            || issue.TechnicalMessage is not { } location
            || BindingLocation().Match(location) is not { Success: true } match)
        {
            return issue;
        }

        // The cooker reports against the scene job's descriptor; a manifest cooks each scene from its own descriptor.
        var scripted = descriptors.Where(static descriptor => descriptor.ScriptBindings.Count > 0).ToArray();
        var descriptor = scripted.FirstOrDefault(candidate => issue.AffectedPath is { } affected && FileSystemPath.From(candidate.DescriptorPath) == FileSystemPath.From(affected))
            ?? (scripted.Length == 1 ? scripted[0] : null);
        var binding = int.Parse(match.Groups["binding"].Value, CultureInfo.InvariantCulture);
        if (descriptor is null || binding >= descriptor.ScriptBindingSites.Count
            || scope.Inputs.FirstOrDefault(input => input.AssetUri == descriptor.SceneAssetUri) is not { } scene)
        {
            return issue;
        }

        var site = descriptor.ScriptBindingSites[binding];
        var parameter = match.Groups["param"].Success && int.Parse(match.Groups["param"].Value, CultureInfo.InvariantCulture) is var index
            && index < site.ParameterNames.Count ? site.ParameterNames[index] : null;
        var subject = parameter is null ? ScriptSlotDiagnostics.Describe(site) : $"parameter '{parameter}' of {ScriptSlotDiagnostics.Describe(site)}";
        return ScriptSlotDiagnostics.Create(
            issue.OperationId,
            issue.Severity,
            issue.Code,
            $"{char.ToUpperInvariant(subject[0])}{subject[1..]}: {issue.Message}",
            scene,
            descriptor.SceneId,
            descriptor.SceneName,
            site,
            parameter,
            technicalMessage: location);
    }

    [GeneratedRegex(@"^bindings\[(?<binding>\d+)\](?:\.params\[(?<param>\d+)\])?", RegexOptions.CultureInvariant, matchTimeoutMilliseconds: 1000)]
    private static partial Regex BindingLocation();
}
