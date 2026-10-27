// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// What the document editors that show a scene share to draw it: they create their viewports here, and outline their
/// selection in the engine's views, without reaching the engine themselves.
/// </summary>
/// <param name="engine">The embedded engine, whose views the viewports draw.</param>
/// <param name="previewSettings">The project's preview preferences, which the toolbar's Settings flyout edits.</param>
/// <param name="engineSettings">The engine's startup settings, whose log verbosity the Settings flyout edits.</param>
/// <param name="operationResults">Publishes what the engine refuses or fails.</param>
/// <param name="statusReducer">Reduces the published results into the status bar.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class ViewportServices(
    IEngineService engine,
    PreviewSettingsService previewSettings,
    ISettingsService<IEngineStartupSettings> engineSettings,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    ILoggerFactory? loggerFactory = null)
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ViewportServices>() ?? NullLogger<ViewportServices>.Instance;

    /// <summary>Creates a viewport that draws a document's scene when the engine shows it.</summary>
    /// <param name="document">The document whose scene the viewport draws.</param>
    /// <returns>The viewport's view model; its owner disposes it.</returns>
    public ViewportViewModel CreateViewport(DocumentId document)
        => new(document, engine, operationResults, statusReducer, loggerFactory);

    /// <summary>Creates what the toolbar's Settings flyout edits: the preview preferences, shared by every editor.</summary>
    /// <returns>The flyout's view model.</returns>
    /// <remarks>The preferences are restored first, so they reach the engine once it runs.</remarks>
    public PreviewSettingsViewModel CreatePreviewSettings()
    {
        _ = previewSettings.EnsureRestoredAsync();
        return new PreviewSettingsViewModel(previewSettings, engineSettings);
    }

    /// <summary>Outlines a document's selection in the engine's views; only the shown document's scene renders.</summary>
    /// <param name="document">The document whose selection it is.</param>
    /// <param name="nodes">The selected nodes.</param>
    /// <param name="activeNode">The selection's primary node, outlined as active.</param>
    /// <param name="scope">What a failure is reported against.</param>
    /// <returns>A task that completes when the engine took the outline, refused it or failed.</returns>
    public async Task OutlineSelectionAsync(DocumentId document, IReadOnlyList<NodeReference> nodes, NodeReference? activeNode, AffectedScope scope)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        try
        {
            if (!await engine.SetSelectionOutlineAsync(nodes, activeNode).ConfigureAwait(true))
            {
                this.LogSelectionOutlineRejected(document);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            operationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = RuntimeOperationKinds.SelectionOutline,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Warning,
                Title = "Selection outline failed",
                Message = "The runtime could not outline the selection.",
                AffectedScope = scope,
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }

    /// <summary>
    /// Shows no transform gizmo in the engine's views, as a viewing pane wants: a document that edits nothing has no
    /// gizmo, whatever another document showed before it.
    /// </summary>
    /// <param name="document">The document that shows no gizmo.</param>
    /// <returns>A task that completes when the engine took the request, refused it or failed.</returns>
    public async Task HideTransformGizmoAsync(DocumentId document)
    {
        var none = new RuntimeTransformGizmo(RuntimeTransformTool.Select, RuntimeTransformSpace.World, default, [], ActiveNode: null, DisplayScale: 1f);
        try
        {
            if (!await engine.SetTransformGizmoAsync(none).ConfigureAwait(true))
            {
                this.LogTransformGizmoRejected(document);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogTransformGizmoFailed(document, ex);
        }
    }
}
