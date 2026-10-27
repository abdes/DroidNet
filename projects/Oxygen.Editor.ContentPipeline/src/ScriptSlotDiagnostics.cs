// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cook diagnostics about a script slot or one of its parameter values, with Go To Property to the slot. A slot of an
/// instanced node is reported on its instance root, the node the scene holds, with the instanced node's reference.
/// </summary>
public static class ScriptSlotDiagnostics
{
    /// <summary>Creates the diagnostic of a slot problem the slot rules report, with Go To Property.</summary>
    /// <param name="operationId">The cook operation.</param>
    /// <param name="problem">The problem.</param>
    /// <param name="scene">The scene being cooked.</param>
    /// <param name="sceneId">The scene identity.</param>
    /// <param name="sceneName">The scene name.</param>
    /// <param name="site">The node and slot.</param>
    /// <returns>The diagnostic, whose message names the node and slot.</returns>
    public static DiagnosticRecord Create(Guid operationId, ScriptSlotProblem problem, ContentCookInput scene, SceneId sceneId, string sceneName, ScriptBindingSite site)
    {
        ArgumentNullException.ThrowIfNull(problem);
        var subject = Describe(site);
        return Create(operationId, problem.Severity, problem.Code, $"{char.ToUpperInvariant(subject[0])}{subject[1..]}: {problem.Message}", scene, sceneId, sceneName, site, problem.Field.Parameter);
    }

    /// <summary>Creates a diagnostic about a slot, with Go To Property.</summary>
    /// <param name="operationId">The cook operation.</param>
    /// <param name="severity">The severity.</param>
    /// <param name="code">The code.</param>
    /// <param name="message">The message, naming the node and slot.</param>
    /// <param name="scene">The scene being cooked.</param>
    /// <param name="sceneId">The scene identity.</param>
    /// <param name="sceneName">The scene name.</param>
    /// <param name="site">The node and slot.</param>
    /// <param name="parameter">The parameter the diagnostic is about, if any.</param>
    /// <param name="technicalMessage">The cooker's message, if the diagnostic comes from it.</param>
    /// <returns>The diagnostic.</returns>
    public static DiagnosticRecord Create(
        Guid operationId,
        DiagnosticSeverity severity,
        string code,
        string message,
        ContentCookInput scene,
        SceneId sceneId,
        string sceneName,
        ScriptBindingSite site,
        string? parameter,
        string? technicalMessage = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(site);
        return new()
        {
            OperationId = operationId,
            Domain = FailureDomain.ScriptAuthoring,
            Severity = severity,
            Code = code,
            Message = message,
            TechnicalMessage = technicalMessage,
            AffectedPath = scene.SourceAbsolutePath.FullPath,
            AffectedVirtualPath = scene.AssetUri.VirtualPath,
            AffectedEntity = new AffectedScope
            {
                SceneId = sceneId,
                SceneName = sceneName,
                NodeId = site.NodeId,
                NodeName = site.NodeName,
                ComponentName = site.Path.IsEmpty ? "Scripting" : "Scene Instance",
                AssetVirtualPath = scene.AssetUri.VirtualPath,
            },
            SuggestedAction = new PrimaryAction
            {
                ActionId = "Cook.GoToProperty",
                Label = "Go to property",
                Kind = PrimaryActionKind.Custom,
                Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["AssetUri"] = scene.AssetUri.ToUriString(),
                    ["NodeId"] = site.NodeId.ToString(),
                    ["NodeReference"] = new NodeReference(site.NodeId, site.Path).ToString(),
                    ["PropertyPath"] = new ScriptFieldAddress(site.SlotId, parameter).ToString(),
                },
            },
        };
    }

    /// <summary>
    /// Names a slot for messages, such as <c>slot 2 of 'Player'</c>, or <c>a script slot of 'Body/Guard' in the instance under 'Tower'</c> for a slot of an instanced node.
    /// </summary>
    /// <param name="site">The node and slot.</param>
    /// <returns>The description.</returns>
    public static string Describe(ScriptBindingSite site)
    {
        ArgumentNullException.ThrowIfNull(site);
        return site.Path.IsEmpty
            ? string.Create(CultureInfo.CurrentCulture, $"slot {site.SlotNumber} of '{site.NodeName}'")
            : string.Create(CultureInfo.CurrentCulture, $"a script slot of '{site.InstancedNames ?? site.Path.ToString()}' in the instance under '{site.NodeName}'");
    }
}
