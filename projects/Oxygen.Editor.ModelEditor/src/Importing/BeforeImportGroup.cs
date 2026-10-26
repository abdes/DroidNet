// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>What the analysis of one selected file found before it is imported.</summary>
/// <param name="FileName">The file's name, the group's header when several files are selected.</param>
/// <param name="Items">The findings the import itself will report, each once with the objects it is about.</param>
/// <param name="ShowsHeader">Whether the group shows its file's name: with several files selected.</param>
public sealed record BeforeImportGroup(string FileName, ImmutableArray<BeforeImportItem> Items, bool ShowsHeader);
