// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>
/// Categorizes geometry assets for display in the asset picker UI.
/// </summary>
public enum GeometryAssetPickerGroup
{
    /// <summary>
    /// Engine-provided built-in assets.
    /// </summary>
    Engine,

    /// <summary>
    /// User/project content assets discovered by the content browser.
    /// </summary>
    Content,
}

/// <summary>
/// Represents a single entry shown in the geometry asset picker.
/// </summary>
/// <param name="Name">The display name of the asset.</param>
/// <param name="Uri">The absolute URI identifying the asset.</param>
/// <param name="DisplayType">A short description of the asset type (for example, "Static Mesh").</param>
/// <param name="DisplayPath">The path shown in the UI for the asset location (for example, "/Content/Models/MyMesh").</param>
/// <param name="Group">The group the asset belongs to (engine or content).</param>
/// <param name="IsEnabled">Whether the asset may be selected/used. Disabled items can be shown but not applied.</param>
/// <param name="ThumbnailModel">An optional model used to render the asset's thumbnail. May be <see langword="null"/>.</param>
public sealed record GeometryAssetPickerItem(
    string Name,
    Uri Uri,
    string DisplayType,
    string DisplayPath,
    GeometryAssetPickerGroup Group,
    bool IsEnabled,
    object? ThumbnailModel);

/// <summary>
/// Represents a named group of <see cref="GeometryAssetPickerItem"/> instances shown in the picker.
/// </summary>
/// <param name="Key">The group key used for identification (for example, "Engine" or "Content").</param>
/// <param name="Items">The items that belong to the group.</param>
public sealed record GeometryAssetGroup(string Key, IReadOnlyList<GeometryAssetPickerItem> Items);

/// <summary>
/// Lightweight header record used to represent a group header in UI lists.
/// </summary>
/// <param name="Title">The title to display for the header.</param>
public sealed record GeometryAssetGroupHeader(string Title);
