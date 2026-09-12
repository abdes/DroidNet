// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>A fixed, explicitly qualified set of build artifacts.</summary>
/// <param name="Version">The manifest contract version.</param>
/// <param name="Configuration">The qualified Debug or Release configuration.</param>
/// <param name="SourceRevision">The source revision recorded by qualification.</param>
/// <param name="Artifacts">Exact file identities and schema versions covered by qualification.</param>
public sealed record ArtifactQualificationManifest(
    int Version,
    string Configuration,
    string SourceRevision,
    ImmutableArray<QualifiedArtifact> Artifacts);
