// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>Keeps an open scene saveable when its file is deleted or moved outside the editor.</summary>
public partial class ProjectManagerService
{
    private readonly System.Collections.Concurrent.ConcurrentDictionary<string, FileVersion> missingSceneBaselines = new(StringComparer.Ordinal);

    /// <inheritdoc/>
    public async Task<bool> RefreshSceneSourcePresenceAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        if (this.GetSceneSourceVersion(scene) is not { } source)
        {
            return false;
        }

        var path = source.SourcePath;
        var current = (await this.AtomicFiles.ReadAsync(path, cancellationToken).ConfigureAwait(true)).Version;
        var expected = this.sceneVersions.GetValueOrDefault(path, FileVersion.Missing);
        if (!current.Exists)
        {
            // A save now creates the file again; a file that reappears meanwhile makes it a conflict.
            if (expected.Exists)
            {
                this.missingSceneBaselines[path] = expected;
                this.sceneVersions[path] = FileVersion.Missing;
                this.SceneSourceMissing(scene.Name, path);
            }

            return true;
        }

        // Restored unchanged, with no save since: the scene is in step with its file again. Restored with other
        // content: saving still expects the file to be missing, so it reports the ordinary external-change conflict.
        if (this.missingSceneBaselines.TryRemove(path, out var known) && !expected.Exists)
        {
            var unchanged = current == known;
            if (unchanged)
            {
                this.sceneVersions[path] = known;
            }

            this.SceneSourceRestored(scene.Name, unchanged);
        }

        return false;
    }
}
