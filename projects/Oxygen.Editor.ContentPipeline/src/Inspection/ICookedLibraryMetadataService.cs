// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Refreshes derived library metadata independently of ordinary status reads.</summary>
public interface ICookedLibraryMetadataService
{
    /// <summary>Inspects changed library generations under the current project writer.</summary>
    /// <param name="project">The project that requested the background refresh.</param>
    /// <param name="cancellationToken">Cancels queued work and drains active native inspection.</param>
    /// <returns>Whether any new dependency metadata was retained.</returns>
    public Task<bool> RefreshLibraryMetadataAsync(ProjectContext project, CancellationToken cancellationToken);
}
