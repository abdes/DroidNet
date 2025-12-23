// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog.FileSystem;

internal enum FileSystemCatalogEventKind
{
    Created,
    Changed,
    Deleted,
    Renamed,
    RescanRequired,
}

internal sealed record FileSystemCatalogEvent(
    FileSystemCatalogEventKind Kind,
    string FullPath,
    string? OldFullPath = null);
