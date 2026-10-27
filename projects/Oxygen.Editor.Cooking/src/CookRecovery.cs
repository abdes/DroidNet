// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking;

/// <summary>
/// Saves what a cook reads, after asking, and shows where a cook's issues and outputs are: the scene property or script
/// slot an issue names, the cooked inspection of a run's scope, and a run's imported assets in the Content Explorer.
/// </summary>
/// <param name="session">The project session the window lives in.</param>
/// <param name="catalog">The project's asset catalog, which lists its scene files.</param>
/// <param name="documents">The window's document area, which asks in its window before it saves.</param>
/// <param name="unsaved">What finds the unsaved documents a cook reads.</param>
/// <param name="placement">Where documents and panels show.</param>
/// <param name="selection">The scene selection every panel follows.</param>
/// <param name="reveal">What shows assets in the Content Explorer.</param>
/// <param name="inspections">What opens the cooked inspection.</param>
public sealed class CookRecovery(
    IProjectSession session,
    IAssetCatalog catalog,
    IDocumentArea documents,
    IUnsavedCookInputs unsaved,
    IPlacementService placement,
    ISceneSelectionService selection,
    IAssetReveal reveal,
    ICookedInspections inspections) : ICookRecovery
{
    /// <summary>The source the selection names.</summary>
    public const string SelectionSource = "Cooking";

    private const string InspectorPanelId = "props";
    private const string CookingPanelId = "cook";

    /// <inheritdoc />
    public Task<bool> SaveInputsAsync(CookRunRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return documents.SaveBeforeAsync(SaveBeforeOperation.Cook, token => unsaved.FindAsync(request, token), session.Lifetime);
    }

    /// <inheritdoc />
    public async Task<bool> GoToPropertyAsync(CookRunSnapshot run, DiagnosticRecord issue)
    {
        ArgumentNullException.ThrowIfNull(run);
        ArgumentNullException.ThrowIfNull(issue);

        // Properties shares a dock with Cooking by default; both stay visible.
        return run.ProjectId == session.Context.ProjectId
            && await this.ShowTargetAsync(issue).ConfigureAwait(true)
            && await placement.ShowPanelsAsync([InspectorPanelId, CookingPanelId]).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<bool> GoToPropertyAsync(DiagnosticRecord issue)
    {
        ArgumentNullException.ThrowIfNull(issue);
        return await this.ShowTargetAsync(issue).ConfigureAwait(true)
            && await placement.ShowPanelsAsync([InspectorPanelId]).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public Task<bool> InspectAsync(CookRunSnapshot run)
    {
        ArgumentNullException.ThrowIfNull(run);
        if (!this.IsActiveProject(run))
        {
            return Task.FromResult(false);
        }

        var scope = run.Request.TargetKind == CookTargetKind.Project ? null : run.Request.ScopeUri;
        return inspections.InspectAsync(new CookedInspectionRequest(scope, Validate: false)
        {
            Asset = run.Request.TargetKind is CookTargetKind.Asset or CookTargetKind.CurrentScene ? scope : null,
        });
    }

    /// <inheritdoc />
    public Task<bool> ShowImportedAssetsAsync(CookRunSnapshot run)
    {
        ArgumentNullException.ThrowIfNull(run);
        return this.IsActiveProject(run) && !run.ImportedOutputs.IsEmpty
            ? reveal.RevealAsync(run.ImportedOutputs)
            : Task.FromResult(false);
    }

    private static bool IsSceneProperty(string property)
        => string.Equals(property, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal)
            || SceneStartingPropertyPaths.IsStartingProperty(property);

    // The node a script slot problem or a kept change names: its inspector shows the slot, or the kept changes.
    private static NodeId? FindNode(DiagnosticRecord issue, string property)
        => (ScriptFieldAddress.TryParse(property, out _) || property.Length == 0 || string.Equals(property, KeptChangeDiagnostics.PropertyPath, StringComparison.Ordinal))
            && issue.SuggestedAction?.Payload.TryGetValue("NodeId", out var text) == true
            && Guid.TryParse(text, CultureInfo.InvariantCulture, out var node)
                ? NodeId.FromValue(node) : null;

    // Opens the scene an issue names and shows its target: a node with its slot or kept changes, or a scene property.
    private async Task<bool> ShowTargetAsync(DiagnosticRecord issue)
    {
        var project = session.Context;
        if (issue.AffectedEntity?.SceneId is not { } sceneId
            || issue.SuggestedAction?.Payload.TryGetValue("PropertyPath", out var property) != true
            || property is null
            || (FindNode(issue, property) is null && !IsSceneProperty(property))
            || await SceneFiles.FindByIdAsync(catalog, project, sceneId, session.Lifetime).ConfigureAwait(true) is not { } scene
            || issue.AffectedPath is not { } sourcePath || !project.FileSystem.File.Exists(sourcePath))
        {
            return false;
        }

        var opened = await placement.OpenDocumentAsync(scene.Identity, cancellationToken: session.Lifetime).ConfigureAwait(true);
        if (opened.Document is not { Content: SceneContent content } document)
        {
            return false;
        }

        if (FindNode(issue, property) is not { } nodeId)
        {
            selection.Publish(document.Id, new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNode: null, PrimaryFolderId: null), SelectionSource);
            selection.RequestPropertyFocus(document.Id, property, SelectionSource);
            return true;
        }

        if (content.Scene.AllNodes.Any(candidate => candidate.Id == nodeId))
        {
            // The node an instance brings in when the issue names one, else the node of the scene.
            var target = issue.SuggestedAction?.Payload.TryGetValue("NodeReference", out var text) == true
                && NodeReference.TryParse(text, out var reference) && reference.Node == nodeId
                    ? reference
                    : NodeReference.To(nodeId);
            selection.SetSelection(document.Id, [target], SelectionSource);
            if (string.Equals(property, KeptChangeDiagnostics.PropertyPath, StringComparison.Ordinal))
            {
                selection.RequestPropertyFocus(document.Id, property, SelectionSource);
            }
        }

        return true;
    }

    private bool IsActiveProject(CookRunSnapshot run)
    {
        var project = session.Context;
        return project.ProjectId == run.ProjectId && project.ProjectRoot == run.ProjectRoot;
    }
}
