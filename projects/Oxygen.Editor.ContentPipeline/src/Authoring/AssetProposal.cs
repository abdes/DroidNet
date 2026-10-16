// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>Where a new script, input file or scene goes and what it is called, before the user changes either.</summary>
/// <param name="Folder">The virtual folder, such as <c>/Content/Scripts</c>.</param>
/// <param name="Name">The first free name, without extension.</param>
public sealed record AssetProposal(string Folder, string Name);
