// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

public enum AssetType
{
    Image,
    Scene,
    Mesh,
    Unknown,
}

public class GameAsset
{
    /// <summary>
    /// Gets or sets the name of the asset.
    /// </summary>
    public string Name { get; set; }

    /// <summary>
    /// Gets or sets the path, relative to the project root, where the asset is located.
    /// </summary>
    public string Location { get; set; }

    /// <summary>
    /// Gets the type of the asset.
    /// </summary>
    public AssetType AssetType { get; init; }

    public GameAsset(string name, string location)
    {
        this.Name = name;
        this.Location = location;
        this.AssetType = GetAssetType(name);
    }

    public static AssetType GetAssetType(string fileName)
    {
        var extension = Path.GetExtension(fileName).ToLowerInvariant();
        return extension switch
        {
            ".png" => AssetType.Image,
            ".scene" => AssetType.Scene,
            ".mesh" => AssetType.Mesh,
            _ => AssetType.Unknown,
        };
    }
}
