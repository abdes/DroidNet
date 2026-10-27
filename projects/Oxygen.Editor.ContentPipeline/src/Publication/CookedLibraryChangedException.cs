// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A published cooked root no longer holds the index recorded by its publication.</summary>
/// <remarks>
/// External libraries are re-cooked outside the editor, so a project publication can name an index
/// that has since been replaced. Republishing the project from its configured libraries resolves it.
/// </remarks>
public sealed class CookedLibraryChangedException : IOException
{
    /// <summary>Initializes a new instance of the <see cref="CookedLibraryChangedException"/> class.</summary>
    public CookedLibraryChangedException()
        : base("A cooked root changed after publication.")
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookedLibraryChangedException"/> class.</summary>
    /// <param name="message">The description of the changed root.</param>
    public CookedLibraryChangedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookedLibraryChangedException"/> class.</summary>
    /// <param name="message">The description of the changed root.</param>
    /// <param name="innerException">The failure that exposed the change.</param>
    public CookedLibraryChangedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookedLibraryChangedException"/> class.</summary>
    /// <param name="root">The published binding.</param>
    /// <param name="path">The resolved physical root.</param>
    /// <param name="actualSourceKey">The source identity found on disk.</param>
    /// <param name="actualIndexSha256">The index digest found on disk.</param>
    public CookedLibraryChangedException(CookPublicationRoot root, string path, Guid actualSourceKey, string actualIndexSha256)
        : base(Describe(root, path, actualSourceKey, actualIndexSha256))
    {
        this.Root = root;
        this.RootPath = path;
        this.ActualSourceKey = actualSourceKey;
        this.ActualIndexSha256 = actualIndexSha256;
    }

    /// <summary>Gets the published binding, or null when the exception was created from a message only.</summary>
    public CookPublicationRoot? Root { get; }

    /// <summary>Gets the resolved physical root.</summary>
    public string? RootPath { get; }

    /// <summary>Gets the source identity found on disk.</summary>
    public Guid? ActualSourceKey { get; }

    /// <summary>Gets the index digest found on disk.</summary>
    public string? ActualIndexSha256 { get; }

    /// <summary>Creates the failure for a root whose index no longer matches the publication that recorded it.</summary>
    /// <param name="root">The published binding.</param>
    /// <param name="path">The resolved physical root.</param>
    /// <param name="actualSourceKey">The source identity found on disk.</param>
    /// <param name="actualIndexSha256">The index digest found on disk.</param>
    /// <returns>
    ///     A <see cref="CookedLibraryChangedException"/> for a library, which publishing the project again resolves; an
    ///     <see cref="InvalidDataException"/> for the project's own output, which is damaged and must not be republished.
    /// </returns>
    internal static Exception ForChangedRoot(CookPublicationRoot root, string path, Guid actualSourceKey, string actualIndexSha256)
    {
        ArgumentNullException.ThrowIfNull(root);
        return root.Owner == CookPublicationRootOwner.Library
            ? new CookedLibraryChangedException(root, path, actualSourceKey, actualIndexSha256)
            : new InvalidDataException($"Cooked project root '{root.Name}' at '{path}' no longer matches its publication: "
                + $"recorded source {root.SourceKey:D} index {root.IndexSha256}, found source {actualSourceKey:D} index {actualIndexSha256}.");
    }

    private static string Describe(CookPublicationRoot root, string path, Guid actualSourceKey, string actualIndexSha256)
    {
        ArgumentNullException.ThrowIfNull(root);
        var owner = root.Owner == CookPublicationRootOwner.Library ? "library" : "project";
        return $"Cooked {owner} root '{root.Name}' at '{path}' changed after publication: "
            + $"recorded source {root.SourceKey:D} index {root.IndexSha256}, found source {actualSourceKey:D} index {actualIndexSha256}. "
            + "Republish the project from its current libraries before using it.";
    }
}
