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
    /// Represents a material asset.
    /// </summary>
    Material,

    /// <summary>
    /// Represents a texture asset.
    /// </summary>
    Texture,

    /// <summary>
    /// Represents cooked data output (e.g. <c>*.data</c>).
    /// </summary>
    CookedData,

    /// <summary>
    /// Represents cooked table output (e.g. <c>*.table</c>).
    /// </summary>
    CookedTable,

    /// <summary>
    /// Represents a folder.
    /// </summary>
    Folder,

    /// <summary>
    /// Represents an import settings file (e.g. <c>*.import</c>).
    /// </summary>
    ImportSettings,

    /// <summary>
    /// Represents a foreign/3rd-party authoring file (e.g. <c>*.glb</c>, <c>*.gltf</c>, <c>*.fbx</c>).
    /// </summary>
    ForeignAsset,

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
    /// Gets or sets the virtual path of the asset (e.g., /Cooked/Folder/Asset).
    /// </summary>
    public string? VirtualPath { get; set; }

    /// <summary>
    /// Gets the type of the asset.
    /// </summary>
    public AssetType AssetType { get; init; }

    /// <summary>
    /// Gets a user-facing display name for <see cref="AssetType"/>.
    /// </summary>
    public string TypeDisplayName
    {
        get
        {
            return this.AssetType switch
            {
                AssetType.Image => BuildTypedDisplayName("Image", this.Location),
                AssetType.ImportSettings => "Import Settings",
                AssetType.CookedData => "Cooked Data",
                AssetType.CookedTable => "Cooked Table",
                AssetType.ForeignAsset => BuildForeignAssetDisplayName(this.Location),
                _ => this.AssetType.ToString(),
            };
        }
    }

    /// <summary>
    /// Determines the asset type based on the file extension.
    /// </summary>
    /// <param name="fileName">The name of the file.</param>
    /// <returns>The <see cref="AssetType"/> corresponding to the file extension.</returns>
    public static AssetType GetAssetType(string fileName)
    {
        var upper = fileName.ToUpperInvariant();

        if (upper.EndsWith(".IMPORT.JSON", StringComparison.Ordinal))
        {
            return AssetType.ImportSettings;
        }

        if (upper.EndsWith(".IMPORT", StringComparison.Ordinal))
        {
            return AssetType.ImportSettings;
        }

        // Oxygen-native authoring sources use compound extensions like "*.omat.json".
        // Treat these as their underlying asset types rather than generic JSON.
        if (upper.EndsWith(".OMAT.JSON", StringComparison.Ordinal))
        {
            return AssetType.Material;
        }

        if (upper.EndsWith(".OGEO.JSON", StringComparison.Ordinal))
        {
            return AssetType.Mesh;
        }

        if (upper.EndsWith(".OSCENE.JSON", StringComparison.Ordinal))
        {
            return AssetType.Scene;
        }

        if (upper.EndsWith(".OTEX.JSON", StringComparison.Ordinal))
        {
            return AssetType.Texture;
        }

        var extension = Path.GetExtension(fileName).ToUpperInvariant();
        return extension switch
        {
            ".PNG" or ".JPG" or ".JPEG" or ".TGA" => AssetType.Image,
            ".OTEX" => AssetType.Texture,
            ".SCENE" or ".OSCENE" => AssetType.Scene,
            ".MESH" or ".OGEO" => AssetType.Mesh,
            ".MAT" or ".OMAT" => AssetType.Material,
            ".DATA" => AssetType.CookedData,
            ".TABLE" => AssetType.CookedTable,
            ".GLB" or ".GLTF" or ".FBX" => AssetType.ForeignAsset,
            _ => AssetType.Unknown,
        };
    }

    private static string BuildForeignAssetDisplayName(string location)
    {
        var ext = Path.GetExtension(location);
        if (string.IsNullOrWhiteSpace(ext))
        {
            return "Foreign Asset";
        }

        ext = ext.TrimStart('.').ToLowerInvariant();
        return $"Foreign Asset ({ext})";
    }

    private static string BuildTypedDisplayName(string baseName, string location)
    {
        var ext = Path.GetExtension(location);
        if (string.IsNullOrWhiteSpace(ext))
        {
            return baseName;
        }

        ext = ext.TrimStart('.').ToLowerInvariant();
        return $"{baseName} ({ext})";
    }
}
