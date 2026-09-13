// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Retained source or the existing document actions that prevent coherent retention.</summary>
/// <param name="Source">The retained bundle, when successful.</param>
/// <param name="NeedsSave">Participating source documents requiring an explicit save.</param>
/// <param name="ExternalChanges">Participating source documents requiring external-change resolution.</param>
public sealed record ImportSourceRetentionResult(RetainedImportSource? Source, ImmutableArray<CookDocumentState> NeedsSave, ImmutableArray<CookDocumentState> ExternalChanges);
