// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A validated authoring namespace for native model output.</summary>
/// <param name="MountName">The output's authoring mount.</param>
/// <param name="OutputDirectory">The exclusive mount-relative model folder.</param>
public sealed record SceneImportTarget(string MountName, string OutputDirectory)
{
    /// <summary>Resolves the reviewed destination without placing output in source or derived folders.</summary>
    /// <param name="project">The reviewed project.</param>
    /// <param name="folder">The destination authoring folder.</param>
    /// <param name="name">The model's unique folder name.</param>
    /// <returns>The validated native destination.</returns>
    public static SceneImportTarget Resolve(ProjectContext project, Uri folder, string name)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(folder);
        if (!folder.IsAbsoluteUri || !string.Equals(folder.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || !string.IsNullOrEmpty(folder.Host) || !string.IsNullOrEmpty(folder.Query) || !string.IsNullOrEmpty(folder.Fragment))
        {
            throw new ArgumentException("Choose a project content folder.", nameof(folder));
        }

        var path = Uri.UnescapeDataString(folder.AbsolutePath).Trim('/');
        var parts = path.Split('/');
        if (string.IsNullOrWhiteSpace(name) || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || name.EndsWith('.') || name.EndsWith(' ')
            || parts.Any(static part => part is "" or "." or ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || part.EndsWith('.') || part.EndsWith(' ')))
        {
            throw new ArgumentException("Use a valid model name and content folder.", nameof(name));
        }

        var mount = project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, parts[0], StringComparison.OrdinalIgnoreCase))
            ?? throw new ArgumentException("Choose an authoring folder, not built-in or cooked content.", nameof(folder));
        if (parts.Skip(1).Any(static part => part.Equals("SourceMedia", StringComparison.OrdinalIgnoreCase)
            || part.Equals(".cooked", StringComparison.OrdinalIgnoreCase) || part.Equals(".imported", StringComparison.OrdinalIgnoreCase)
            || part.Equals(".build", StringComparison.OrdinalIgnoreCase) || part.Equals(".pipeline", StringComparison.OrdinalIgnoreCase)))
        {
            throw new ArgumentException("Choose a content destination outside source-media and generated folders.", nameof(folder));
        }

        var directory = string.Join('/', parts.Skip(1).Append(name));
        return new(mount.Name, directory);
    }
}
