// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>The file a document edits: where it is on disk and the asset it is.</summary>
/// <param name="Path">The file on disk.</param>
/// <param name="Asset">The asset the file is the source of.</param>
public sealed record DocumentFile(FileSystemPath Path, AssetPath Asset);
