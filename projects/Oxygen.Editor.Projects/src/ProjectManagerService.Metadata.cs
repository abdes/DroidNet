// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>Commits complete project metadata without truncating the accepted manifest or overwriting an external edit.</summary>
public partial class ProjectManagerService
{
    private Task<bool> SaveProjectInfoCoreAsync(IProjectInfo projectInfo, IProjectInfo? expected, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(projectInfo);
        if (string.IsNullOrWhiteSpace(projectInfo.Location))
        {
            throw new ArgumentException("The project location is required.", nameof(projectInfo));
        }

        var path = storage.NormalizeRelativeTo(projectInfo.Location, Constants.ProjectFileName);
        return this.sceneWrites.RunAsync(
            path,
            async () =>
            {
                var snapshot = await this.AtomicFiles.ReadAsync(path, cancellationToken).ConfigureAwait(true);
                if (expected is not null && (!snapshot.Version.Exists
                    || !string.Equals(ProjectInfo.ToJson(ProjectInfo.FromJson(Encoding.UTF8.GetString(snapshot.Content.AsSpan()).TrimStart('\uFEFF'))), ProjectInfo.ToJson(expected), StringComparison.Ordinal)))
                {
                    throw new IOException("The project file changed outside the editor. Reload the project before changing its content mounts.");
                }

                _ = await this.AtomicFiles.WriteAsync(path, Encoding.UTF8.GetBytes(ProjectInfo.ToJson(projectInfo)), snapshot.Version, cancellationToken).ConfigureAwait(true);
                return true;
            },
            cancellationToken);
    }
}
