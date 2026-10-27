// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Durable boundaries of one source and publication-head transaction.</summary>
internal enum CookPublicationPhase
{
    /// <summary>Candidate generations are reserved and being built; nothing is selected yet.</summary>
    Building,

    /// <summary>The candidate head and source changes are captured and validated.</summary>
    Prepared,

    /// <summary>Applying has begun; source directories and files may be partly replaced.</summary>
    Applying,

    /// <summary>Every source directory and file change is installed.</summary>
    SourcesApplied,

    /// <summary>The candidate publication head is written as the selected head.</summary>
    HeadSelected,

    /// <summary>The candidate generations are mounted and ready for readers.</summary>
    RuntimeReady,

    /// <summary>The transaction completed; the candidate is the published state.</summary>
    Committed,

    /// <summary>The transaction was abandoned or undone; the previous state is current.</summary>
    RolledBack,
}

/// <summary>Owns candidate generations and the small before/after state needed for recovery.</summary>
internal sealed record CookPublicationJournal(
    int Version,
    Guid ProjectId,
    Guid OperationId,
    CookPublicationPhase Phase,
    ImmutableArray<Guid> CandidateGenerations,
    FileSnapshot PreviousHead,
    FileSnapshot? CandidateHead)
{
    internal const int CurrentVersion = 4;

    public SourceBundle? SourceReplacement { get; init; }

    public ImmutableArray<CookProducedSourceFile> SourceFiles { get; init; } = [];

    public ProjectConfiguration? ProjectChange { get; init; }

    /// <summary>A source-directory replacement retains its authored before/after identities.</summary>
    public sealed record SourceBundle(string BundleRoot, CookRootImage Before, CookRootImage After);

    /// <summary>Project-manager representations used for the owner's configuration CAS and recovery.</summary>
    public sealed record ProjectConfiguration(string BeforeJson, string AfterJson);
}
