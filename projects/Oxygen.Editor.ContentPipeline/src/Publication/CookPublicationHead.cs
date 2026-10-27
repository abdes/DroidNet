// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>The sole mutable selection of an immutable publication document.</summary>
/// <param name="Version">The head's format version.</param>
/// <param name="PublicationId">The identity of the selected publication document.</param>
/// <param name="DocumentSha256">The SHA-256 digest of the selected document's bytes.</param>
internal sealed record CookPublicationHead(int Version, Guid PublicationId, string DocumentSha256)
{
    internal const int CurrentVersion = 1;
}
