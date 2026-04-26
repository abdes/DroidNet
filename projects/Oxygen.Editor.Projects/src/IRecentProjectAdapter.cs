// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Adapts persistent recent-project records into validated project entries.
/// </summary>
public interface IRecentProjectAdapter
{
    /// <summary>
    ///     Gets recent projects with validation state.
    /// </summary>
    /// <param name="sizeLimit">The maximum number of recent records to inspect.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>Validated recent project entries.</returns>
    public IAsyncEnumerable<RecentProjectEntry> GetRecentProjectsAsync(
        uint sizeLimit = 10,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Records successful project activation.
    /// </summary>
    /// <param name="context">The activated project context.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task representing the operation.</returns>
    public Task RecordActivatedAsync(ProjectContext context, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Removes a recent project record after explicit user action.
    /// </summary>
    /// <param name="name">The stored project name.</param>
    /// <param name="location">The stored project location.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task representing the operation.</returns>
    public Task RemoveAsync(string name, string location, CancellationToken cancellationToken = default);
}
