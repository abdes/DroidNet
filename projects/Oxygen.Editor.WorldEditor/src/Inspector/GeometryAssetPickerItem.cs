// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Inspector;

public enum GeometryAssetPickerGroup
{
    Engine,
    Content,
}

public sealed record GeometryAssetPickerItem(
    string Name,
    Uri Uri,
    string DisplayType,
    string DisplayPath,
    GeometryAssetPickerGroup Group,
    bool IsEnabled,
    object? ThumbnailModel);

public sealed record GeometryAssetGroup(string Key, IReadOnlyList<GeometryAssetPickerItem> Items);

public sealed record GeometryAssetGroupHeader(string Title);
