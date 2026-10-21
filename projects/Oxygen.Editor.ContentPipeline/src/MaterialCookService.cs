// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Routes material cooking through the shared saved-input pipeline.</summary>
/// <param name="pipeline">The shared cook, compatibility and snapshot workflow.</param>
/// <param name="session">The project session, whose project's materials the service cooks.</param>
/// <param name="logger">Material workflow logging.</param>
public sealed partial class MaterialCookService(
    IContentPipelineService pipeline,
    IProjectSession session,
    ILogger<MaterialCookService> logger) : IMaterialCookService
{
    private readonly IContentPipelineService pipeline = pipeline ?? throw new ArgumentNullException(nameof(pipeline));
    private readonly IProjectSession session = session ?? throw new ArgumentNullException(nameof(session));
    private readonly ILogger<MaterialCookService> logger = logger ?? throw new ArgumentNullException(nameof(logger));

    /// <inheritdoc />
    public async Task<MaterialCookResult> CookMaterialAsync(MaterialCookRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        var project = this.session.Context;
        if (!IsCurrentMaterialRequest(request, project))
        {
            this.LogRejected(request.Material);
            return new(request.Material, CookedMaterialUri: null, MaterialCookState.Rejected, OperationId: null);
        }

        var result = await this.pipeline.CookAssetAsync(request.Material, cancellationToken, project).ConfigureAwait(false);
        var cooked = result.CookedAssets.Concat(result.ReusedAssets).FirstOrDefault(asset => asset.Kind == ContentCookAssetKind.Material
            && asset.CookedAssetUri == request.Material);
        var succeeded = result.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings;
        var state = !succeeded || cooked is null ? MaterialCookState.Failed
            : result.InputsAreCurrent == false ? MaterialCookState.Stale : MaterialCookState.Cooked;
        this.LogCompleted(request.Material, result.OperationId, state);
        return new(request.Material, cooked?.CookedAssetUri, state, result.OperationId) { Cook = result };
    }

    private static bool IsCurrentMaterialRequest(MaterialCookRequest request, ProjectContext project)
    {
        if (request.Material is null
            || request.ProjectRoot is null || string.IsNullOrWhiteSpace(request.MountName)
            || string.IsNullOrWhiteSpace(request.SourceRelativePath) || Path.IsPathRooted(request.SourceRelativePath))
        {
            return false;
        }

        try
        {
            if (request.ProjectRoot != project.ProjectRoot)
            {
                return false;
            }

            var input = CookInputResolver.Resolve(project, request.Material, ContentCookInputRole.Primary);
            return input.Kind == ContentCookAssetKind.Material
                && string.Equals(input.MountName, request.MountName, StringComparison.Ordinal)
                && input.SourceAbsolutePath == project.ProjectRoot.Combine(request.SourceRelativePath);
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidDataException)
        {
            return false;
        }
    }
}
