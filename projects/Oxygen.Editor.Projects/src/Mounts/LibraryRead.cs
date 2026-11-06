// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Persistence.Publication;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// What a library's binding reads to: a loose cooked root, a pak, or the generation a library project's <c>.cooked</c>
/// folder selects, and the facts of that content. The mount table reads a declared library through it, and the Add
/// library form reads one that is not mounted yet.
/// </summary>
/// <param name="Location">What is read: the generation of a <c>.cooked</c> binding, else the binding itself.</param>
/// <param name="Container">How the content is stored.</param>
/// <param name="Facts">What the content holds.</param>
public sealed record LibraryRead(FileSystemPath Location, MountContainer Container, LibraryFacts Facts)
{
    /// <summary>Reads a library that is not mounted yet, whose name comes from its content.</summary>
    /// <param name="fileSystem">The file system.</param>
    /// <param name="bound">The pak file, cooked folder or <c>.cooked</c> folder.</param>
    /// <param name="findings">Receives a finding's code and its text when the library cannot be read.</param>
    /// <returns>What was read; <see langword="null"/> when it cannot be read.</returns>
    public static LibraryRead? Inspect(IFileSystem fileSystem, FileSystemPath bound, Action<string, string> findings)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(bound);
        return Of(fileSystem, bound, fileSystem.Directory.Exists(bound.FullPath), root: null, findings);
    }

    /// <summary>Reads a library.</summary>
    /// <param name="fileSystem">The file system.</param>
    /// <param name="bound">The pak file, cooked folder or <c>.cooked</c> folder.</param>
    /// <param name="isFolder">Whether it is a folder.</param>
    /// <param name="root">The root a <c>.cooked</c> folder is asked for; <see langword="null"/> takes its one root.</param>
    /// <param name="findings">Receives a finding's code and its text when the library cannot be read.</param>
    /// <returns>What was read; <see langword="null"/> when it cannot be read.</returns>
    internal static LibraryRead? Of(IFileSystem fileSystem, FileSystemPath bound, bool isFolder, string? root, Action<string, string> findings)
    {
        try
        {
            return !isFolder ? OfPak(fileSystem, bound)
                : fileSystem.File.Exists(bound.Combine(CookPublicationFormat.HeadFileName).FullPath) ? OfGeneration(fileSystem, bound, root, findings)
                : OfLooseRoot(fileSystem, bound, MountContainer.LooseRoot);
        }
        catch (Exception exception) when (exception is InvalidDataException or IOException or UnauthorizedAccessException or NotSupportedException)
        {
            findings(MountCodes.CookedFormatUnsupported, $"cannot be read: {exception.Message}");
            return null;
        }
    }

    private static LibraryRead OfPak(IFileSystem fileSystem, FileSystemPath file)
    {
        using var stream = fileSystem.File.OpenRead(file.FullPath);
        return new(file, MountContainer.Pak, LibraryFacts.Of(PakIndex.Read(stream)));
    }

    private static LibraryRead OfLooseRoot(IFileSystem fileSystem, FileSystemPath folder, MountContainer container)
    {
        var indexPath = folder.Combine(LooseCookedIndex.FileName).FullPath;
        if (!fileSystem.File.Exists(indexPath))
        {
            throw new InvalidDataException("it is neither a loose cooked root, a pak nor a project's cooked output");
        }

        using var stream = fileSystem.File.OpenRead(indexPath);
        return new(folder, container, LibraryFacts.Of(LooseCookedIndex.Read(stream), relative => fileSystem.File.ReadAllBytes(folder.Combine(relative).FullPath)));
    }

    // A library project's output folder: head.json selects the publication, whose document selects the root's generation.
    private static LibraryRead? OfGeneration(IFileSystem fileSystem, FileSystemPath cooked, string? root, Action<string, string> findings)
    {
        var head = CookPublicationFormat.ReadHead(fileSystem.File.ReadAllBytes(cooked.Combine(CookPublicationFormat.HeadFileName).FullPath));
        var document = fileSystem.File.ReadAllBytes(cooked.Combine(CookPublicationFormat.DocumentPath(head.PublicationId)).FullPath);
        if (root is null)
        {
            var roots = CookPublicationFormat.ProjectRoots(head, document);
            if (roots.Count != 1)
            {
                findings(MountCodes.CookedRootMultiple, $"publishes the roots '{string.Join("', '", roots)}': a cooked source has one root");
                return null;
            }

            root = roots[0];
        }

        if (CookPublicationFormat.SelectGeneration(head, document, root) is not { } sourceKey)
        {
            findings(MountCodes.CookedRootMismatch, $"selects no generation for the root '{root}': a cooked source's one root is its mount name");
            return null;
        }

        return OfLooseRoot(fileSystem, cooked.Combine(CookPublicationFormat.GenerationPath(sourceKey)), MountContainer.Generation);
    }
}
