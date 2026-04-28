// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Message sent when authoring assets were created, removed, or rewritten outside normal watcher delivery.
/// </summary>
/// <param name="AssetUri">The changed asset URI when the caller knows it.</param>
public sealed record AssetsChangedMessage(Uri? AssetUri = null);
