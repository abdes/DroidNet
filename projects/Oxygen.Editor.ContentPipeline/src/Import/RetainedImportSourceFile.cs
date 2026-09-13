// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>One retained source file and the hash of its coherent captured bytes.</summary>
/// <param name="RelativePath">The file's location within the bundle.</param>
/// <param name="Sha256">The retained content hash.</param>
public sealed record RetainedImportSourceFile(string RelativePath, string Sha256);
