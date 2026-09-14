// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>The existing retained source and byte baseline explicitly reviewed for replacement.</summary>
public sealed class SceneImportReplacement
{
    private SceneImportReplacement(string bundleName, Uri sourceUri, NativeSceneImportSettings settings, CookRootImage before)
    {
        this.BundleName = bundleName;
        this.SourceUri = sourceUri;
        this.Settings = settings;
        this.Before = before;
    }

    /// <summary>Gets the retained-source directory name.</summary>
    public string BundleName { get; }

    /// <summary>Gets the stable source identity preserved by replacement.</summary>
    public Uri SourceUri { get; }

    /// <summary>Gets the existing import policies and output namespace.</summary>
    public NativeSceneImportSettings Settings { get; }

    /// <summary>Gets the reviewed source and settings byte identities.</summary>
    internal CookRootImage Before { get; }

    /// <summary>Identifies one exclusively owned retained model without changing any source or output.</summary>
    /// <param name="project">The reviewed project.</param>
    /// <param name="bundleName">The colliding retained-source directory name.</param>
    /// <param name="cancellationToken">Cancels read-only review.</param>
    /// <returns>The exact replacement target.</returns>
    public static async Task<SceneImportReplacement> ReviewAsync(ProjectContext project, string bundleName, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var root = ImportSourceRetention.ResolveDestination(project, bundleName);
        var before = await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false);
        var sidecars = before.Files.Keys.Where(static path => path.EndsWith(NativeSceneImportSettings.SidecarSuffix, StringComparison.OrdinalIgnoreCase)).ToArray();
        if (!before.Exists || sidecars.Length != 1)
        {
            throw new InvalidDataException("The existing folder must contain one configured model. Choose another name or import the existing source in place.");
        }

        var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(Path.Combine(root, sidecars[0]), cancellationToken).ConfigureAwait(false));
        var relativeRoot = Path.GetRelativePath(project.ProjectRoot, root).Replace('\\', '/');
        if (!string.Equals(relativeRoot, settings.BundleRoot, StringComparison.OrdinalIgnoreCase)
            || !string.Equals(sidecars[0], settings.PrimaryRelativePath + NativeSceneImportSettings.SidecarSuffix, StringComparison.OrdinalIgnoreCase)
            || !project.AuthoringMounts.Any(mount => string.Equals(mount.Name, settings.MountPoint, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidDataException("The existing import settings do not own this retained source and destination.");
        }

        var owned = settings.Files.Append(sidecars[0]).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var unrelated = before.Files.Keys.FirstOrDefault(path => !owned.Contains(path));
        if (unrelated is not null)
        {
            throw new InvalidDataException($"The retained folder also contains '{unrelated}', which this import does not own. Move it before replacing this source.");
        }

        var after = await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false);
        if (!before.Matches(after))
        {
            throw new IOException("The retained source changed during review. Review it again.");
        }

        var mountName = project.AuthoringMounts.Single(static mount => string.Equals(mount.Name, "Content", StringComparison.OrdinalIgnoreCase)).Name;
        var virtualPath = string.Join('/', new[] { mountName, "SourceMedia", "DCC", bundleName }.Concat(settings.PrimaryRelativePath.Split('/')).Select(Uri.EscapeDataString));
        return new(bundleName, new Uri(AssetUris.Scheme + ":///" + virtualPath), settings, before);
    }
}
