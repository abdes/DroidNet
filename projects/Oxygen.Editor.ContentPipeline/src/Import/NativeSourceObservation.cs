// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A native read or probe to compare against captured source state.</summary>
/// <param name="Path">The absolute logical source path.</param>
/// <param name="Exists">The observed presence or absence.</param>
/// <param name="Metadata">Original metadata when native preparation queried it.</param>
/// <param name="Reads">Consumed byte ranges, independent of the capture's whole-file digest.</param>
public sealed record NativeSourceObservation(string Path, bool Exists, NativeSourceFileMetadata? Metadata, ImmutableArray<NativeSourceReadProof> Reads);
