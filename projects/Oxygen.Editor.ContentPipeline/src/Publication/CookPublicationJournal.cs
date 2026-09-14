// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Recoverable phases of a multi-root publication.</summary>
internal enum CookPublicationPhase
{
    /// <summary>Validated staging and prior identities are recorded.</summary>
    Prepared,

    /// <summary>All prior roots have been retained for restoration.</summary>
    OldRetained,

    /// <summary>All new roots are installed at their published locations.</summary>
    RootsInstalled,

    /// <summary>The current runtime has accepted the new roots.</summary>
    RuntimeReady,

    /// <summary>Metadata and preview agree with the new publication.</summary>
    Committed,

    /// <summary>Prior roots and metadata have been restored.</summary>
    RolledBack,
}

/// <summary>Contains content identities and prior metadata, without accepting arbitrary filesystem paths.</summary>
/// <param name="Version">The journal format.</param>
/// <param name="ProjectId">The owning project.</param>
/// <param name="OperationId">The directory-owning operation.</param>
/// <param name="Phase">The last durably recorded phase.</param>
/// <param name="Roots">Every root replaced by this operation.</param>
/// <param name="Metadata">Prior and proposed publication/cache metadata.</param>
internal sealed record CookPublicationJournal(
    int Version,
    Guid ProjectId,
    Guid OperationId,
    CookPublicationPhase Phase,
    ImmutableArray<CookPublicationJournal.Root> Roots,
    ImmutableArray<CookPublicationJournal.MetadataFile> Metadata)
{
    /// <summary>Gets the reviewed retained-source directory installed with this generation.</summary>
    public SourceBundle? SourceReplacement { get; init; }

    /// <summary>A retained source bundle and the complete before/after directory identities.</summary>
    /// <param name="BundleName">The single retained-source directory name.</param>
    /// <param name="Before">The reviewed original source and settings.</param>
    /// <param name="After">The privately captured replacement source and settings.</param>
    public sealed record SourceBundle(string BundleName, CookRootImage Before, CookRootImage After);

    /// <summary>Identities before and after native staging validation.</summary>
    /// <param name="Mount">One physical mount directory name.</param>
    /// <param name="Before">The previously published content.</param>
    /// <param name="After">The validated private content.</param>
    public sealed record Root(string Mount, CookRootImage Before, CookRootImage After);

    /// <summary>A bounded editor-owned metadata file restored alongside the roots.</summary>
    /// <param name="RelativePath">The publication receipt or product provenance cache.</param>
    /// <param name="Before">Previous bytes; null means the file was absent.</param>
    /// <param name="After">The proposed bytes.</param>
    public sealed record MetadataFile(string RelativePath, byte[]? Before, byte[] After);
}
