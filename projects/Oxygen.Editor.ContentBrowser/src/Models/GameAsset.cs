// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Models;

/// <summary>
/// Specifies the type of a game asset.
/// </summary>
public enum AssetType
{
    /// <summary>
    /// Represents an image asset.
    /// </summary>
    Image,

    /// <summary>
    /// Represents a scene asset.
    /// </summary>
    Scene,

    /// <summary>
    /// Represents a mesh asset.
    /// </summary>
    Mesh,

    /// <summary>
    /// Represents a folder.
    /// </summary>
    Folder,

    /// <summary>
    /// Represents an unknown asset type.
    /// </summary>
    Unknown,
}

/// <summary>
/// Represents a game asset with a name, location, and type.
/// </summary>
public class GameAsset
{
    /// <summary>
    /// Initializes a new instance of the <see cref="GameAsset"/> class.
    /// </summary>
    /// <param name="name">The name of the asset.</param>
    /// <param name="location">The path, relative to the project root, where the asset is located.</param>
    public GameAsset(string name, string location)
    {
        this.Name = name;
        this.Location = location;
        this.AssetType = GetAssetType(name);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="GameAsset"/> class with a specified asset type.
    /// </summary>
    /// <param name="name">The name of the asset.</param>
    /// <param name="location">The path, relative to the project root, where the asset is located.</param>
    /// <param name="assetType">The type of the asset.</param>
    public GameAsset(string name, string location, AssetType assetType)
    {
        this.Name = name;
        this.Location = location;
        this.AssetType = assetType;
    }

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

    /// <summary>
    /// Determines the asset type based on the file extension.
    /// </summary>
    /// <param name="fileName">The name of the file.</param>
    /// <returns>The <see cref="AssetType"/> corresponding to the file extension.</returns>
    public static AssetType GetAssetType(string fileName)
    {
        var extension = Path.GetExtension(fileName).ToUpperInvariant();
        return extension switch
        {
            ".PNG" => AssetType.Image,
            ".SCENE" => AssetType.Scene,
            ".MESH" => AssetType.Mesh,
            _ => AssetType.Unknown,
        };
    }
}
