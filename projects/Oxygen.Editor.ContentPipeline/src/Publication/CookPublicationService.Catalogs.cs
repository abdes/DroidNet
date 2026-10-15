// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <inheritdoc cref="CookPublicationService"/>
public sealed partial class CookPublicationService
{
    /// <inheritdoc />
    public async Task<CookPublicationCatalogs> AcquireCatalogsAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var selected = await this.AcquireReadAsync(project, cancellationToken).ConfigureAwait(false);
        try
        {
            return new(selected, await selected.CreateCatalogsAsync(cancellationToken).ConfigureAwait(false));
        }
        catch (CookedLibraryChangedException changed)
        {
            LogCookedRootChanged(this.Logger, changed, selected.PublicationId, project.ProjectRoot);
            selected.Dispose();
        }
        catch
        {
            selected.Dispose();
            throw;
        }

        return await this.RepublishCatalogsAsync(project, cancellationToken).ConfigureAwait(false);
    }

    private async Task<CookPublicationCatalogs> RepublishCatalogsAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        CookPublicationReadLease republished;
        try
        {
            republished = await this.AcquireForMountAsync(project, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException)
        {
            LogRepublishFailed(this.Logger, failure, project.ProjectRoot);
            throw;
        }

        try
        {
            var catalogs = await republished.CreateCatalogsAsync(cancellationToken).ConfigureAwait(false);
            LogRepublished(this.Logger, republished.PublicationId, project.ProjectRoot);
            return new(republished, catalogs);
        }
        catch
        {
            republished.Dispose();
            throw;
        }
    }
}
