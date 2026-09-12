// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Routes material cooking through the shared saved-input pipeline.</summary>
/// <param name="pipeline">The shared cook, qualification and snapshot workflow.</param>
/// <param name="projectContextService">The active project context.</param>
/// <param name="logger">Material workflow logging.</param>
public sealed partial class MaterialCookService(
    IContentPipelineService pipeline,
    IProjectContextService projectContextService,
    ILogger<MaterialCookService> logger) : IMaterialCookService
{
    private readonly IContentPipelineService pipeline = pipeline ?? throw new ArgumentNullException(nameof(pipeline));
    private readonly IProjectContextService projectContextService = projectContextService ?? throw new ArgumentNullException(nameof(projectContextService));
    private readonly ILogger<MaterialCookService> logger = logger ?? throw new ArgumentNullException(nameof(logger));

    /// <inheritdoc />
    public async Task<MaterialCookResult> CookMaterialAsync(MaterialCookRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        if (!this.IsCurrentMaterialRequest(request, out var project))
        {
            this.LogRejected(request.MaterialSourceUri);
            return new(request.MaterialSourceUri, CookedMaterialUri: null, MaterialCookState.Rejected, OperationId: null);
        }

        var result = await this.pipeline.CookAssetAsync(request.MaterialSourceUri, cancellationToken, project).ConfigureAwait(false);
        var cooked = result.CookedAssets.FirstOrDefault(asset => asset.Kind == ContentCookAssetKind.Material
            && asset.CookedAssetUri == GetCookedUri(request.MaterialSourceUri));
        var succeeded = result.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings;
        var state = !succeeded || cooked is null ? MaterialCookState.Failed
            : result.InputsAreCurrent == false ? MaterialCookState.Stale : MaterialCookState.Cooked;
        this.LogCompleted(request.MaterialSourceUri, result.OperationId, state);
        return new(request.MaterialSourceUri, cooked?.CookedAssetUri, state, result.OperationId) { Cook = result };
    }

    private bool IsCurrentMaterialRequest(MaterialCookRequest request, out ProjectContext? project)
    {
        project = this.projectContextService.ActiveProject;
        if (project is null
            || request.MaterialSourceUri is not { IsAbsoluteUri: true }
            || string.IsNullOrWhiteSpace(request.ProjectRoot) || string.IsNullOrWhiteSpace(request.MountName)
            || !Path.IsPathFullyQualified(request.ProjectRoot)
            || string.IsNullOrWhiteSpace(request.SourceRelativePath) || Path.IsPathRooted(request.SourceRelativePath))
        {
            return false;
        }

        try
        {
            if (!string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(request.ProjectRoot)), Path.TrimEndingDirectorySeparator(project.ProjectRoot), StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            var input = CookInputResolver.Resolve(project, request.MaterialSourceUri, ContentCookInputRole.Primary);
            return input.Kind == ContentCookAssetKind.Material
                && string.Equals(input.MountName, request.MountName, StringComparison.OrdinalIgnoreCase)
                && string.Equals(input.SourceAbsolutePath, Path.GetFullPath(Path.Combine(project.ProjectRoot, request.SourceRelativePath)), StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidDataException)
        {
            return false;
        }
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cooked material state could not be inspected for {MaterialUri}.")]
    private partial void LogStateInspectionFailed(Uri materialUri, Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Material cook rejected for {MaterialUri}: the source identity does not match the active project.")]
    private partial void LogRejected(Uri materialUri);

    [LoggerMessage(Level = LogLevel.Information, Message = "Material cook {OperationId} completed for {MaterialUri}: {State}.")]
    private partial void LogCompleted(Uri materialUri, Guid operationId, MaterialCookState state);
}
