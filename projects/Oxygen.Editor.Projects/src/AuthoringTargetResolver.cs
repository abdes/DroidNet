// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Project-layout implementation of authored asset creation target rules. Targets are bound source mounts,
///     inside or outside the project tree; an unbound or missing mount is never a target.
/// </summary>
public sealed class AuthoringTargetResolver : IAuthoringTargetResolver
{
    /// <inheritdoc />
    public AuthoringTarget ResolveCreateTarget(
        ProjectContext project,
        AuthoringAssetKind assetKind,
        ContentFolderSelection? selection)
    {
        ArgumentNullException.ThrowIfNull(project);

        var kindFolder = GetKindFolder(assetKind);
        var selected = NormalizeProjectPath(selection?.ProjectRelativeFolder);
        var defaultMount = GetDefaultMount(project);

        if (!string.IsNullOrWhiteSpace(selection?.LocalMountName))
        {
            var selectedMount = project.SourceMounts.FirstOrDefault(
                mount => string.Equals(mount.Name, selection.LocalMountName, StringComparison.Ordinal));
            if (selectedMount is not null)
            {
                var mountRelativeSelection = GetMountRelativeSelection(selectedMount.Name, selected);
                if (string.IsNullOrWhiteSpace(mountRelativeSelection))
                {
                    return CreateTarget(selectedMount.Name, kindFolder, isExplicit: true, fallbackReason: null);
                }

                return IsKindFolderSelection(selectedMount.Name, mountRelativeSelection, kindFolder)
                    ? CreateTarget(selectedMount.Name, mountRelativeSelection, isExplicit: true, fallbackReason: null)
                    : CreateTarget(selectedMount.Name, kindFolder, isExplicit: true, AuthoringTargetFallbackReason.KindMismatch);
            }

            return CreateTarget(defaultMount, kindFolder, isExplicit: false, AuthoringTargetFallbackReason.UnknownLocalMount);
        }

        if (string.IsNullOrWhiteSpace(selected))
        {
            return CreateTarget(defaultMount, kindFolder, isExplicit: false, AuthoringTargetFallbackReason.NoSelection);
        }

        return ResolveSourceMountTarget(project, selected, kindFolder, defaultMount);
    }

    // The selection is a folder of the project on disk; the innermost bound source mount that holds it decides.
    private static AuthoringTarget ResolveSourceMountTarget(
        ProjectContext project,
        string selected,
        string kindFolder,
        string defaultMount)
    {
        var folder = project.ProjectRoot.Combine(selected);
        foreach (var mount in project.SourceMounts.OrderByDescending(static mount => mount.Location!.FullPath.Length))
        {
            var mountRoot = mount.Location!;
            if (folder == mountRoot)
            {
                return CreateTarget(mount.Name, kindFolder, isExplicit: false, fallbackReason: null);
            }

            if (folder.IsSameOrInside(mountRoot))
            {
                var mountRelative = folder.GetPathBelow(mountRoot);
                return IsKindFolderSelection(mount.Name, mountRelative, kindFolder)
                    ? CreateTarget(mount.Name, mountRelative, isExplicit: false, fallbackReason: null)
                    : CreateTarget(mount.Name, kindFolder, isExplicit: false, AuthoringTargetFallbackReason.KindMismatch);
            }
        }

        return CreateTarget(defaultMount, kindFolder, isExplicit: false, AuthoringTargetFallbackReason.NonAuthoringSelection);
    }

    private static string GetKindFolder(AuthoringAssetKind assetKind)
        => assetKind switch
        {
            AuthoringAssetKind.Scene => Constants.ScenesFolderName,
            AuthoringAssetKind.Material => "Materials",
            AuthoringAssetKind.Geometry => "Geometry",
            AuthoringAssetKind.Texture => "Textures",
            AuthoringAssetKind.Audio => "Audio",
            AuthoringAssetKind.Video => "Video",
            AuthoringAssetKind.Script => "Scripts",
            AuthoringAssetKind.Prefab => "Prefabs",
            AuthoringAssetKind.Animation => "Animations",
            _ => throw new ArgumentOutOfRangeException(nameof(assetKind), assetKind, "Unknown authoring asset kind."),
        };

    // The Content mount when it is bound, else the first bound source mount.
    private static string GetDefaultMount(ProjectContext project)
        => project.SourceMounts.FirstOrDefault(static mount => string.Equals(mount.Name, Constants.ContentFolderName, StringComparison.Ordinal))?.Name
            ?? project.SourceMounts.FirstOrDefault()?.Name
            ?? Constants.ContentFolderName;

    private static AuthoringTarget CreateTarget(
        string mountName,
        string mountRelativeFolder,
        bool isExplicit,
        AuthoringTargetFallbackReason? fallbackReason)
        => new(
            mountName,
            AssetPath.Create(mountName, mountRelativeFolder),
            isExplicit,
            fallbackReason);

    private static string GetMountRelativeSelection(string mountName, string selected)
    {
        if (string.IsNullOrWhiteSpace(selected)
            || string.Equals(selected, mountName, StringComparison.Ordinal))
        {
            return string.Empty;
        }

        return selected.StartsWith(mountName + "/", StringComparison.Ordinal)
            ? selected[(mountName.Length + 1)..]
            : selected;
    }

    private static bool IsKindFolderSelection(string mountName, string mountRelative, string kindFolder)
        => AssetPath.TryCreate(mountName, mountRelative, out var folder) && folder.IsSameOrInside(AssetPath.Create(mountName, kindFolder));

    private static string NormalizeProjectPath(string? path)
        => (path ?? string.Empty).Replace('\\', '/').Trim().Trim('/');
}
