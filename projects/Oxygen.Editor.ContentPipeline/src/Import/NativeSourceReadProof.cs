// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Digest of the bytes native preparation consumed.</summary>
/// <param name="Offset">Starting file byte.</param>
/// <param name="MaxBytes">Maximum bytes consumed; zero extends to EOF.</param>
/// <param name="Sha256">Lowercase SHA-256 of that range.</param>
public sealed record NativeSourceReadProof(ulong Offset, ulong MaxBytes, string Sha256);
