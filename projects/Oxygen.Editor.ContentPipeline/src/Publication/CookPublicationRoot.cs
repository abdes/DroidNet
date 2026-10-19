// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Distinguishes project-owned generations from read-only external libraries.</summary>
public enum CookPublicationRootOwner
{
    /// <summary>A project-owned immutable generation.</summary>
    Project,

    /// <summary>An external loose-cooked library observed at its configured path.</summary>
    Library,
}

/// <summary>How a library stores its cooked content.</summary>
public enum CookPublicationRootContainer
{
    /// <summary>A loose cooked root directory.</summary>
    LooseFolder,

    /// <summary>A pak file.</summary>
    PakFile,
}

/// <summary>One binding in the accepted native source order.</summary>
/// <param name="Owner">The root's ownership and location policy.</param>
/// <param name="Name">The logical project mount or library name.</param>
/// <param name="SourceKey">The native source identity; also the owned generation's directory identity.</param>
/// <param name="IndexSha256">The observed content identity: the loose index digest, or a pak's catalog digest.</param>
/// <param name="LibraryPath">The absolute location of a library; null for project-owned roots.</param>
public sealed record CookPublicationRoot(CookPublicationRootOwner Owner, string Name, Guid SourceKey, string IndexSha256, string? LibraryPath)
{
    /// <summary>Gets how the root stores its content; project roots and publications written before paks are loose folders.</summary>
    public CookPublicationRootContainer Container { get; init; } = CookPublicationRootContainer.LooseFolder;

    internal string ResolvePath(string projectRoot) => this.Owner switch
    {
        CookPublicationRootOwner.Project when this.LibraryPath is null => CookPublicationPaths.Generation(projectRoot, this.SourceKey),
        CookPublicationRootOwner.Library when this.LibraryPath is not null => this.LibraryPath,
        _ => throw new InvalidDataException("Invalid cooked-root location."),
    };
}
