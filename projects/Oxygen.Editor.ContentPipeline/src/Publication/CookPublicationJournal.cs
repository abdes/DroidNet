// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Durable boundaries of one source and publication-head transaction.</summary>
internal enum CookPublicationPhase
{
    Building,
    Prepared,
    Applying,
    SourcesApplied,
    HeadSelected,
    RuntimeReady,
    Committed,
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
    internal const int CurrentVersion = 3;

    public SourceBundle? SourceReplacement { get; init; }

    public ImmutableArray<CookProducedSourceFile> SourceFiles { get; init; } = [];

    public ProjectConfiguration? ProjectChange { get; init; }

    /// <summary>A source-directory replacement retains its authored before/after identities.</summary>
    public sealed record SourceBundle(string BundleName, CookRootImage Before, CookRootImage After);

    /// <summary>Project-manager representations used for the owner's configuration CAS and recovery.</summary>
    public sealed record ProjectConfiguration(string BeforeJson, string AfterJson);
}
