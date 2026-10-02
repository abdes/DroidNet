// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Last observed producer identity for UI freshness, never an artifact lease.</summary>
public sealed record NativeProducerObservation(long Revision, string? Fingerprint, ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets the initial state before discovery or a native operation.</summary>
    public static NativeProducerObservation Unknown { get; } = new(0, null, []);
}
