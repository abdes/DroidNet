// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Immutable source admission facts sent to the native cooker.</summary>
/// <param name="Inputs">Logical paths and their captured bytes or frozen probes.</param>
public sealed record NativeCapturedInputSet(ImmutableArray<NativeCapturedInput> Inputs)
{
    /// <summary>Gets the native capture-contract version.</summary>
    public int SchemaVersion { get; } = 1;
}
