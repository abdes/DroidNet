// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>One file's portable identity in the native SDK receipt.</summary>
/// <param name="Id">A stable logical path, independent of installation directories.</param>
/// <param name="Size">The compatible file length in bytes.</param>
/// <param name="Sha256">The compatible SHA-256 content hash.</param>
/// <param name="SchemaId">The supported schema identifier, for schema artifacts.</param>
public sealed record NativeArtifact(string Id, long Size, string Sha256, string? SchemaId = null);
