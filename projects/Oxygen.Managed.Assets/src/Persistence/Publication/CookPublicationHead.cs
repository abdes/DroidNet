// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.Publication;

/// <summary>What a project's <c>head.json</c> holds: the publication that is current.</summary>
/// <param name="Version">The head's format version.</param>
/// <param name="PublicationId">The current publication.</param>
/// <param name="DocumentSha256">The digest of the publication's document, in hexadecimal.</param>
public sealed record CookPublicationHead(int Version, Guid PublicationId, string DocumentSha256)
{
    /// <summary>The head format this editor reads and writes.</summary>
    public const int CurrentVersion = 1;
}
