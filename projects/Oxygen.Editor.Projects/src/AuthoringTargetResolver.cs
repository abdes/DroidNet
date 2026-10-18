// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Project-layout implementation of authored asset creation target rules.
/// </summary>
public sealed class AuthoringTargetResolver : IAuthoringTargetResolver
{
    /// <inheritdoc />
    public AuthoringTarget ResolveCreateTarget(
        ProjectContext project,
        AuthoringAssetKind assetKind,
        ContentBrowserSelection? selection)
    {
        ArgumentNullException.ThrowIfNull(project);

        var kindFolder = GetKindFolder(assetKind);
        var selected = NormalizeProjectPath(selection?.ProjectRelativeFolder);
        var defaultMount = GetDefaultMount(project);

        if (!string.IsNullOrWhiteSpace(selection?.LocalMountName))
        {
            var localMount = project.LocalFolderMounts.FirstOrDefault(
                mount => string.Equals(mount.Name, selection.LocalMountName, StringComparison.Ordinal));
            if (localMount is not null)
            {
                var localRelativeSelection = GetLocalMountRelativeSelection(localMount.Name, selected);
                if (string.IsNullOrWhiteSpace(localRelativeSelection))
                {
                    return CreateLocalMountTarget(localMount.Name, kindFolder, fallbackReason: null);
                }

                if (IsKindFolderSelection(localMount.Name, localRelativeSelection, kindFolder))
                {
                    return CreateLocalMountTarget(localMount.Name, localRelativeSelection, fallbackReason: null);
                }

                return CreateLocalMountTarget(localMount.Name, kindFolder, AuthoringTargetFallbackReason.KindMismatch);
            }

            return CreateProjectMountTarget(defaultMount, kindFolder, AuthoringTargetFallbackReason.UnknownLocalMount);
        }

        if (string.IsNullOrWhiteSpace(selected))
        {
            return CreateProjectMountTarget(defaultMount, kindFolder, AuthoringTargetFallbackReason.NoSelection);
        }

        return ResolveAuthoringMountTarget(project, selected, kindFolder, defaultMount);
    }

    // The selection is a folder of the project on disk; the innermost authoring mount that holds it decides.
    private static AuthoringTarget ResolveAuthoringMountTarget(
        ProjectContext project,
        string selected,
        string kindFolder,
        ProjectMountPoint defaultMount)
    {
        var root = project.ProjectRoot;
        var folder = root.Combine(selected);
        foreach (var mount in project.AuthoringMounts.OrderByDescending(static mount => mount.RelativePath.Length))
        {
            if (string.IsNullOrWhiteSpace(NormalizeProjectPath(mount.RelativePath)))
            {
                continue;
            }

            var mountRoot = root.Combine(mount.RelativePath);
            if (folder == mountRoot)
            {
                return CreateProjectMountTarget(mount, kindFolder, fallbackReason: null);
            }

            if (folder.IsSameOrInside(mountRoot))
            {
                var mountRelative = folder.GetPathBelow(mountRoot);
                return IsKindFolderSelection(mount.Name, mountRelative, kindFolder)
                    ? new(mount.Name, selected, AssetPath.Create(mount.Name, mountRelative), IsExplicitLocalMount: false, FallbackReason: null)
                    : CreateProjectMountTarget(mount, kindFolder, AuthoringTargetFallbackReason.KindMismatch);
            }
        }

        return CreateProjectMountTarget(defaultMount, kindFolder, AuthoringTargetFallbackReason.NonAuthoringSelection);
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

    private static ProjectMountPoint GetDefaultMount(ProjectContext project)
    {
        ProjectMountPoint? first = null;
        foreach (var mount in project.AuthoringMounts)
        {
            first ??= mount;
            if (string.Equals(mount.Name, Constants.ContentFolderName, StringComparison.Ordinal))
            {
                return mount;
            }
        }

        return first ?? new ProjectMountPoint(Constants.ContentFolderName, Constants.ContentFolderName);
    }

    private static AuthoringTarget CreateProjectMountTarget(
        ProjectMountPoint mount,
        string kindFolder,
        AuthoringTargetFallbackReason? fallbackReason)
        => new(
            mount.Name,
            CombineProjectPath(NormalizeProjectPath(mount.RelativePath), kindFolder),
            AssetPath.Create(mount.Name, kindFolder),
            IsExplicitLocalMount: false,
            fallbackReason);

    private static AuthoringTarget CreateLocalMountTarget(
        string mountName,
        string mountRelativeFolder,
        AuthoringTargetFallbackReason? fallbackReason)
        => new(
            mountName,
            mountRelativeFolder,
            AssetPath.Create(mountName, mountRelativeFolder),
            IsExplicitLocalMount: true,
            fallbackReason);

    private static string GetLocalMountRelativeSelection(string mountName, string selected)
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

    private static string CombineProjectPath(string left, string right)
    {
        if (string.IsNullOrWhiteSpace(left))
        {
            return NormalizeProjectPath(right);
        }

        return string.IsNullOrWhiteSpace(right)
            ? left
            : left.TrimEnd('/') + "/" + right.Trim('/');
    }

    private static string NormalizeProjectPath(string? path)
        => (path ?? string.Empty).Replace('\\', '/').Trim().Trim('/');
}
