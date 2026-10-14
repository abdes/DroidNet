// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cook diagnostics about a script slot or one of its parameter values, with Go To Property to the slot.
/// </summary>
public static class ScriptSlotDiagnostics
{
    /// <summary>The prefix of a script slot property path: <c>Scripting/{slot}</c> or <c>Scripting/{slot}/{parameter}</c>.</summary>
    public const string PropertyPathPrefix = "Scripting/";

    /// <summary>Gets the property path of a slot or one of its parameters.</summary>
    /// <param name="slotId">The slot.</param>
    /// <param name="parameter">The parameter name, or null for the slot itself.</param>
    /// <returns>The property path.</returns>
    public static string PropertyPath(Guid slotId, string? parameter)
        => PropertyPathPrefix + slotId.ToString("D") + (parameter is null ? string.Empty : "/" + parameter);

    /// <summary>Parses a script slot property path.</summary>
    /// <param name="path">The property path.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="parameter">The parameter name, or null for the slot itself.</param>
    /// <returns>Whether the path names a script slot.</returns>
    public static bool TryParsePropertyPath(string? path, out Guid slotId, out string? parameter)
    {
        slotId = Guid.Empty;
        parameter = null;
        if (path is null || !path.StartsWith(PropertyPathPrefix, StringComparison.Ordinal))
        {
            return false;
        }

        var parts = path[PropertyPathPrefix.Length..].Split('/', 2);
        if (!Guid.TryParse(parts[0], CultureInfo.InvariantCulture, out slotId))
        {
            return false;
        }

        parameter = parts.Length > 1 ? parts[1] : null;
        return true;
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
        Guid sceneId,
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
            AffectedPath = scene.SourceAbsolutePath,
            AffectedVirtualPath = scene.AssetUri.AbsolutePath,
            AffectedEntity = new AffectedScope
            {
                SceneId = sceneId,
                SceneName = sceneName,
                NodeId = site.NodeId,
                NodeName = site.NodeName,
                ComponentName = "Scripting",
                AssetVirtualPath = scene.AssetUri.AbsolutePath,
            },
            SuggestedAction = new PrimaryAction
            {
                ActionId = "Cook.GoToProperty",
                Label = "Go to property",
                Kind = PrimaryActionKind.Custom,
                Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["AssetUri"] = scene.AssetUri.AbsoluteUri,
                    ["NodeId"] = site.NodeId.ToString("D"),
                    ["PropertyPath"] = PropertyPath(site.SlotId, parameter),
                },
            },
        };
    }

    /// <summary>Names a slot for messages, such as <c>slot 2 of 'Player'</c>.</summary>
    /// <param name="site">The node and slot.</param>
    /// <returns>The description.</returns>
    public static string Describe(ScriptBindingSite site)
    {
        ArgumentNullException.ThrowIfNull(site);
        return string.Create(CultureInfo.CurrentCulture, $"slot {site.SlotNumber} of '{site.NodeName}'");
    }
}
