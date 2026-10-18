// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Turns a committed relocation into what one open document follows.</summary>
public static class DocumentRelocations
{
    /// <summary>Gets the document's file after the relocation, the version the relocation wrote, and its references' mapping.</summary>
    /// <param name="document">The open document.</param>
    /// <param name="change">The committed relocation.</param>
    /// <param name="project">The project whose mounts hold the document's file.</param>
    /// <param name="skipped">Why the document has nothing to follow, when it has nothing to follow.</param>
    /// <returns>What the document follows, or <see langword="null"/> when the relocation neither moved nor rewrote its file.</returns>
    public static DocumentRelocation? Map(Document document, AssetRelocationChange change, ProjectContext project, out string? skipped)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(change);
        ArgumentNullException.ThrowIfNull(project);
        var path = document.File.Path;
        var target = change.MapPath(path) ?? path;
        var rewrite = change.FindRewrite(target) ?? change.FindRewrite(path);
        if (rewrite is null && target == path)
        {
            skipped = "the relocation did not move or rewrite " + path;
            return null;
        }

        if (project.FindByFilePath(target) is not { } location)
        {
            skipped = "its file moved out of the project's mounts to " + target;
            return null;
        }

        skipped = null;
        return new DocumentRelocation(new DocumentFile(location.File, location.Identity), rewrite?.Written ?? document.Version, change.MapReference);
    }
}
