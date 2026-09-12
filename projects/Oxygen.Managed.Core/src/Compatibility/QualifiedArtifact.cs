// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>One file's portable identity in the accepted qualification manifest.</summary>
/// <param name="Id">A stable logical path, independent of installation directories.</param>
/// <param name="Size">The qualified file length in bytes.</param>
/// <param name="Sha256">The qualified SHA-256 content hash.</param>
/// <param name="SchemaId">The supported schema identifier, for schema artifacts.</param>
public sealed record QualifiedArtifact(string Id, long Size, string Sha256, string? SchemaId = null);
