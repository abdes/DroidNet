// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core;
using Oxygen.Editor.World;

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
                mount => string.Equals(mount.Name, selection.LocalMountName, StringComparison.OrdinalIgnoreCase));
            if (localMount is not null)
            {
                var localRelativeSelection = GetLocalMountRelativeSelection(localMount.Name, selected);
                if (string.IsNullOrWhiteSpace(localRelativeSelection))
                {
                    return CreateLocalMountTarget(localMount.Name, kindFolder, null);
                }

                if (IsKindFolderSelection(localRelativeSelection, kindFolder))
                {
                    return CreateLocalMountTarget(localMount.Name, localRelativeSelection, null);
                }

                return CreateLocalMountTarget(localMount.Name, kindFolder, AuthoringTargetFallbackReason.KindMismatch);
            }

            return CreateProjectMountTarget(defaultMount, kindFolder, AuthoringTargetFallbackReason.UnknownLocalMount);
        }

        if (string.IsNullOrWhiteSpace(selected))
        {
            return CreateProjectMountTarget(defaultMount, kindFolder, AuthoringTargetFallbackReason.NoSelection);
        }

        foreach (var mount in project.AuthoringMounts.OrderByDescending(static mount => mount.RelativePath.Length))
        {
            var mountPath = NormalizeProjectPath(mount.RelativePath);
            if (string.IsNullOrWhiteSpace(mountPath))
            {
                continue;
            }

            if (string.Equals(selected, mountPath, StringComparison.OrdinalIgnoreCase))
            {
                return CreateProjectMountTarget(mount, kindFolder, null);
            }

            if (selected.StartsWith(mountPath + "/", StringComparison.OrdinalIgnoreCase))
            {
                var mountRelative = selected[(mountPath.Length + 1)..];
                if (IsKindFolderSelection(mountRelative, kindFolder))
                {
                    return CreateProjectMountTarget(mount.Name, selected, ToAssetFolderUri(mount.Name, mountRelative), false, null);
                }

                return CreateProjectMountTarget(mount, kindFolder, AuthoringTargetFallbackReason.KindMismatch);
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
            if (string.Equals(mount.Name, Constants.ContentFolderName, StringComparison.OrdinalIgnoreCase))
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
    {
        var mountPath = NormalizeProjectPath(mount.RelativePath);
        var projectRelativeFolder = CombineProjectPath(mountPath, kindFolder);
        return CreateProjectMountTarget(
            mount.Name,
            projectRelativeFolder,
            ToAssetFolderUri(mount.Name, kindFolder),
            false,
            fallbackReason);
    }

    private static AuthoringTarget CreateProjectMountTarget(
        string mountName,
        string projectRelativeFolder,
        Uri folderAssetUri,
        bool isExplicitLocalMount,
        AuthoringTargetFallbackReason? fallbackReason)
        => new(
            mountName,
            projectRelativeFolder,
            folderAssetUri,
            isExplicitLocalMount,
            fallbackReason);

    private static AuthoringTarget CreateLocalMountTarget(
        string mountName,
        string mountRelativeFolder,
        AuthoringTargetFallbackReason? fallbackReason)
        => CreateProjectMountTarget(
            mountName,
            mountRelativeFolder,
            ToAssetFolderUri(mountName, mountRelativeFolder),
            true,
            fallbackReason);

    private static string GetLocalMountRelativeSelection(string mountName, string selected)
    {
        if (string.IsNullOrWhiteSpace(selected)
            || string.Equals(selected, mountName, StringComparison.OrdinalIgnoreCase))
        {
            return string.Empty;
        }

        return selected.StartsWith(mountName + "/", StringComparison.OrdinalIgnoreCase)
            ? selected[(mountName.Length + 1)..]
            : selected;
    }

    private static bool IsKindFolderSelection(string mountRelative, string kindFolder)
        => string.Equals(mountRelative, kindFolder, StringComparison.OrdinalIgnoreCase)
           || mountRelative.StartsWith(kindFolder + "/", StringComparison.OrdinalIgnoreCase);

    private static Uri ToAssetFolderUri(string mountName, string mountRelative)
    {
        var relative = NormalizeProjectPath(mountRelative);
        return new Uri($"{AssetUris.Scheme}:///{mountName}/{relative}".TrimEnd('/'));
    }

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
