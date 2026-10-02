// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Derives owned publication paths from identities, never serialized filesystem paths.</summary>
internal static class CookPublicationPaths
{
    internal const string HeadRelativePath = ".cooked/head.json";

    internal static string Head(string projectRoot) => OwnedPath(projectRoot, ".cooked", "head.json");

    internal static string Document(string projectRoot, Guid publicationId)
        => OwnedPath(projectRoot, ".cooked", "publications", Identity(publicationId) + ".json");

    internal static string Generation(string projectRoot, Guid sourceKey)
        => OwnedPath(projectRoot, ".cooked", "generations", Identity(sourceKey));

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
