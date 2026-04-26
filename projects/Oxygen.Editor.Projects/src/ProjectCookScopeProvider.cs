// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Default project cook scope provider.
/// </summary>
/// <param name="storage">The storage provider used for path normalization.</param>
public sealed class ProjectCookScopeProvider(IStorageProvider storage) : IProjectCookScopeProvider
{
    /// <inheritdoc/>
    public ProjectCookScope CreateScope(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        return new ProjectCookScope(
            context.ProjectId,
            context.ProjectRoot,
            storage.NormalizeRelativeTo(context.ProjectRoot, Constants.CookedOutputFolderName));
    }
}
