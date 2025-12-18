// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents a notification of an asset change (added, removed, or modified).
/// </summary>
/// <param name="ChangeType">The type of change that occurred.</param>
/// <param name="Asset">The asset that changed.</param>
/// <param name="Timestamp">When the change occurred.</param>
public record AssetChangeNotification(
    AssetChangeType ChangeType,
    GameAsset Asset,
    DateTimeOffset Timestamp);
