// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Records the native SDK inputs used by an ordinary Interop build.</summary>
/// <param name="Version">The receipt format version.</param>
/// <param name="Configuration">The Debug or Release build configuration.</param>
/// <param name="Artifacts">The native SDK binary identities.</param>
public sealed record NativeBuildReceipt(
    int Version,
    string Configuration,
    ImmutableArray<NativeArtifact> Artifacts);
