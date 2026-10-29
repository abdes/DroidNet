// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Derives owned publication and cook operation paths from identities, never serialized filesystem paths.</summary>
internal static class CookPublicationPaths
{
    private const string PublicationsFolder = "publications";
    private const string GenerationsFolder = "generations";

    internal static string Head(string projectRoot) => OwnedPath(projectRoot, ProjectOutputFolders.Cooked, "head.json");

    internal static string Document(string projectRoot, Guid publicationId)
        => OwnedPath(projectRoot, ProjectOutputFolders.Cooked, PublicationsFolder, Identity(publicationId) + ".json");

    internal static string Generation(string projectRoot, Guid sourceKey)
        => OwnedPath(projectRoot, ProjectOutputFolders.Cooked, GenerationsFolder, Identity(sourceKey));

    /// <summary>The folder that holds the publication documents.</summary>
    internal static string Publications(string projectRoot) => ProjectOutputFolders.CookedPath(projectRoot, PublicationsFolder);

    /// <summary>The folder that holds the mount generations.</summary>
    internal static string Generations(string projectRoot) => ProjectOutputFolders.CookedPath(projectRoot, GenerationsFolder);

    /// <summary>The folder of every cook operation, with the publication and maintenance locks.</summary>
    internal static string Operations(string projectRoot) => ProjectOutputFolders.BuildPath(projectRoot, "cook");

    /// <summary>The folder of one cook operation: its locks, journal and inputs.</summary>
    internal static string Operation(string projectRoot, Guid operationId) => Path.Combine(Operations(projectRoot), operationId.ToString("N"));

    private static string Identity(Guid value) => value != Guid.Empty ? value.ToString("N")
        : throw new InvalidDataException("Publication and generation identities must not be empty.");

    private static string OwnedPath(string projectRoot, params string[] segments)
    {
        var path = Path.GetFullPath(projectRoot);
        CookOutputLease.RejectReparsePoint(path);
        foreach (var segment in segments)
        {
            path = Path.Combine(path, segment);
            CookOutputLease.RejectReparsePoint(path);
        }

        return path;
    }
}
