// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>
/// Finds which buffer container produces a buffer. A geometry names its buffers, which have no file of their own and need
/// not sit under the container, so only the containers say who owns them.
/// </summary>
internal static class BufferContainers
{
    /// <summary>Reads the buffer containers of a project's source mounts.</summary>
    /// <param name="project">The project.</param>
    /// <returns>Each container's identity, by the virtual path of each buffer it names. A container that cannot be read
    /// names none; a geometry that needs one of its buffers reports that buffer missing.</returns>
    public static IReadOnlyDictionary<string, AssetPath> ProductsOf(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var products = new Dictionary<string, AssetPath>(StringComparer.Ordinal);
        foreach (var mount in project.SourceMounts.Where(static mount => mount.IsBound))
        {
            var root = mount.Location!;
            var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = true };
            foreach (var file in project.FileSystem.Directory.EnumerateFiles(root.FullPath, "*" + AssetFileTypes.BufferContainer, options).Select(FileSystemPath.From))
            {
                var below = file.GetPathBelow(root);
                if (!below.Split('/').Any(static segment => segment.StartsWith('.')) && AssetPath.TryCreate(mount.Name, below, out var container))
                {
                    AddProducts(project, file, container, products);
                }
            }
        }

        return products;
    }

    private static void AddProducts(ProjectContext project, FileSystemPath file, AssetPath container, Dictionary<string, AssetPath> products)
    {
        try
        {
            var identity = AssetFileTypes.ToIdentity(container);
            foreach (var product in AuthoredReferenceFormat.ReadBufferProducts(container, project.FileSystem.File.ReadAllBytes(file.FullPath)))
            {
                products[product.VirtualPath] = identity;
            }
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or JsonException)
        {
            // The geometry that needs one of its buffers reports that buffer missing.
        }
    }
}
