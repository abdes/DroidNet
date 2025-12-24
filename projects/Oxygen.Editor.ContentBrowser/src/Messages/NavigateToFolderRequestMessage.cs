// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
///     A message requesting navigation to a specific folder in the content browser. This allows
///     other ViewModels (like AssetsViewModel) to request navigation that will be handled by the
///     ProjectLayoutViewModel to ensure correct virtual path resolution and tree selection.
/// </summary>
/// <param name="Folder">The folder to navigate to.</param>
public record NavigateToFolderRequestMessage(IFolder Folder);
