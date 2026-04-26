// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Oxygen.Editor.Data.Services;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Default recent-project adapter.
/// </summary>
/// <param name="projectUsage">The persistent project usage service.</param>
/// <param name="validation">The project validation service.</param>
public sealed class RecentProjectAdapter(
    IProjectUsageService projectUsage,
    IProjectValidationService validation) : IRecentProjectAdapter
{
    /// <inheritdoc/>
    public async IAsyncEnumerable<RecentProjectEntry> GetRecentProjectsAsync(
        uint sizeLimit = 10,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        var records = await projectUsage.GetMostRecentlyUsedProjectsAsync(sizeLimit).ConfigureAwait(false);
        foreach (var record in records)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var validationResult = await validation.ValidateAsync(record.Location, cancellationToken)
                .ConfigureAwait(false);
            if (validationResult.ProjectInfo is { } projectInfo)
            {
                projectInfo.LastUsedOn = record.LastUsedOn;
            }

            yield return new RecentProjectEntry
            {
                Name = record.Name,
                Location = record.Location,
                LastUsedOn = record.LastUsedOn,
                Validation = validationResult,
            };
        }
    }

    /// <inheritdoc/>
    public async Task RecordActivatedAsync(
        ProjectContext context,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(context);
        cancellationToken.ThrowIfCancellationRequested();

        await projectUsage.UpdateProjectUsageAsync(context.Name, context.ProjectRoot).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task RemoveAsync(
        string name,
        string location,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await projectUsage.DeleteProjectUsageAsync(name, location).ConfigureAwait(false);
    }
}
