// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
/// Represents progress information during asset indexing.
/// </summary>
/// <param name="FoldersScanned">Number of folders scanned so far.</param>
/// <param name="AssetsFound">Number of assets discovered so far.</param>
/// <param name="CurrentFolder">The folder currently being scanned, if any.</param>
public record IndexingProgress(
    int FoldersScanned,
    int AssetsFound,
    string? CurrentFolder);
