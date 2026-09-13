// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Verified source ownership and the dependencies of a cooked product.</summary>
/// <param name="CookedAssetUri">The inspected output identity.</param>
/// <param name="SourceAssetUri">The authoring or engine origin.</param>
/// <param name="Dependencies">Logical dependencies of the captured published product.</param>
public sealed record CookedAssetProvenance(Uri CookedAssetUri, Uri SourceAssetUri, IReadOnlyList<Uri> Dependencies);
