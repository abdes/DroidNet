// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Read-only material output inspection for editor state queries.</summary>
public sealed partial class MaterialCookService
{
    /// <inheritdoc />
    public async Task<MaterialCookState> GetMaterialCookStateAsync(
        Uri materialSourceUri,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(materialSourceUri);
        cancellationToken.ThrowIfCancellationRequested();
        if (this.projectContextService.ActiveProject is not { } project)
        {
            return MaterialCookState.NotCooked;
        }

        try
        {
            var states = await this.pipeline.ReadAsync(project, [materialSourceUri], cancellationToken).ConfigureAwait(false);
            var state = states.Single();
            return state.Freshness is AssetCookFreshness.InvalidSource or AssetCookFreshness.MissingSource ? MaterialCookState.Failed
                : !state.HasPublishedOutput ? MaterialCookState.NotCooked
                : !state.HasVerifiedOutput ? MaterialCookState.Failed
                : state.Freshness == AssetCookFreshness.Current && !state.HasUnsavedChanges ? MaterialCookState.Cooked
                : MaterialCookState.Stale;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or NotSupportedException or ArgumentException)
        {
            this.LogStateInspectionFailed(materialSourceUri, exception);
            return MaterialCookState.Failed;
        }
    }

    private static Uri GetCookedUri(Uri materialSourceUri)
    {
        var path = materialSourceUri.AbsolutePath;
        if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            path = path[..^".json".Length];
        }

        return new Uri($"{AssetUris.Scheme}://{path}");
    }
}
