// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>What became of a request to open an asset's document.</summary>
public sealed record DocumentPlacement
{
    /// <summary>Gets the requested asset.</summary>
    public required AssetPath Asset { get; init; }

    /// <summary>Gets the document that is shown, or <see langword="null"/> when the request failed or was cancelled.</summary>
    public Document? Document { get; init; }

    /// <summary>
    /// Gets the assets of the documents that closed to make room for this one, in a kind that allows one document per
    /// area. They closed even when the requested document then failed to show.
    /// </summary>
    public IReadOnlyList<AssetPath> Replaced { get; init; } = [];

    /// <summary>Gets why the document is not shown, or <see langword="null"/> when it is shown or the user cancelled.</summary>
    public string? Problem { get; init; }

    /// <summary>Gets a value indicating whether the document is shown.</summary>
    public bool Succeeded => this.Document is not null;
}
