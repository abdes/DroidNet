// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A portable source bundle retained before derived import work begins.</summary>
/// <param name="DirectoryRelativePath">The bundle directory relative to the project.</param>
/// <param name="PrimaryRelativePath">The selected source relative to the bundle.</param>
/// <param name="Files">The retained source/dependency identities relative to the bundle.</param>
public sealed record RetainedImportSource(string DirectoryRelativePath, string PrimaryRelativePath, ImmutableArray<RetainedImportSourceFile> Files);
