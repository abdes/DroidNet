// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Mounting;

/// <summary>Applies a confirmed change of the project's content mounts, while no cook publishes.</summary>
/// <remarks>The project's preview implements it: it saves the change, mounts the content and shows it.</remarks>
public interface IContentMountChanges
{
    /// <summary>Applies a confirmed content-mount change.</summary>
    /// <param name="expected">The project context accepted when editing began.</param>
    /// <param name="candidate">The candidate configuration.</param>
    /// <param name="cancellationToken">Cancels queued or uncommitted work.</param>
    /// <returns>
    /// <see langword="true"/> once the change is saved, mounted and active; <see langword="false"/> when another project
    /// became active before the saved change could be shown.
    /// </returns>
    public Task<bool> ChangeMountsAsync(ProjectContext expected, ProjectInfo candidate, CancellationToken cancellationToken = default);
}
