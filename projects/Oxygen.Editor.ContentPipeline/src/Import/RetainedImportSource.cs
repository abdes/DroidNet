// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A portable source bundle retained before derived import work begins.</summary>
/// <param name="BundleRoot">The bundle folder's cook path, such as <c>/Content/Models/Ship</c>.</param>
/// <param name="PrimaryRelativePath">The selected source relative to the bundle.</param>
/// <param name="Files">The retained source/dependency identities relative to the bundle.</param>
public sealed record RetainedImportSource(string BundleRoot, string PrimaryRelativePath, ImmutableArray<RetainedImportSourceFile> Files);
