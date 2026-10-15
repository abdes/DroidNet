// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets;

/// <summary>
/// The file types of the asset pipeline and what each cooks into, shared by the editor, the cooker and the tools
/// around them.
/// </summary>
/// <remarks>
/// The cooked extensions mirror <c>oxygen::content::import::LooseCookedLayout</c>. An authored descriptor is named
/// after the descriptor it cooks into, followed by <see cref="DescriptorSuffix"/>.
/// </remarks>
public static class AssetFileTypes
{
    /// <summary>The suffix an authored descriptor adds to the cooked descriptor it cooks into.</summary>
    public const string DescriptorSuffix = ".json";

    /// <summary>The cooked material extension.</summary>
    public const string Material = ".omat";

    /// <summary>The cooked geometry extension.</summary>
    public const string Geometry = ".ogeo";

    /// <summary>The cooked scene extension.</summary>
    public const string Scene = ".oscene";

    /// <summary>The cooked texture extension.</summary>
    public const string Texture = ".otex";

    /// <summary>The cooked script extension.</summary>
    public const string Script = ".oscript";

    /// <summary>The cooked input action extension.</summary>
    public const string InputAction = ".oiact";

    /// <summary>The cooked input mapping context extension.</summary>
    public const string InputMappingContext = ".oimap";

    /// <summary>The cooked physics scene sidecar extension.</summary>
    public const string PhysicsScene = ".opscene";

    /// <summary>The cooked physics material extension.</summary>
    public const string PhysicsMaterial = ".opmat";

    /// <summary>The cooked collision shape extension.</summary>
    public const string CollisionShape = ".ocshape";

    /// <summary>The cooked buffer descriptor extension.</summary>
    public const string Buffer = ".obuf";

    /// <summary>The cooked physics resource descriptor extension.</summary>
    public const string PhysicsResource = ".opres";

    /// <summary>The authored material descriptor suffix.</summary>
    public const string MaterialDescriptor = Material + DescriptorSuffix;

    /// <summary>The authored geometry descriptor suffix.</summary>
    public const string GeometryDescriptor = Geometry + DescriptorSuffix;

    /// <summary>The authored scene descriptor suffix.</summary>
    public const string SceneDescriptor = Scene + DescriptorSuffix;

    /// <summary>The authored texture descriptor suffix.</summary>
    public const string TextureDescriptor = Texture + DescriptorSuffix;

    /// <summary>The authored physics scene sidecar descriptor suffix.</summary>
    public const string PhysicsSceneDescriptor = PhysicsScene + DescriptorSuffix;

    /// <summary>The authored physics material descriptor suffix.</summary>
    public const string PhysicsMaterialDescriptor = PhysicsMaterial + DescriptorSuffix;

    /// <summary>The authored collision shape descriptor suffix.</summary>
    public const string CollisionShapeDescriptor = CollisionShape + DescriptorSuffix;

    /// <summary>The Lua script source suffix.</summary>
    public const string LuaScript = ".lua";

    /// <summary>The Luau script source suffix.</summary>
    public const string LuauScript = ".luau";

    /// <summary>The suffix of an input file, which declares actions and mapping contexts.</summary>
    public const string InputFile = ".input.json";

    /// <summary>The suffix of an input file that declares actions only.</summary>
    public const string InputActionFile = ".input-action.json";

    /// <summary>The suffix of the import settings stored beside a model.</summary>
    public const string ImportSettings = ".import.json";

    private static readonly (string Extension, AssetType Type)[] CookedDescriptors =
    [
        (Material, AssetType.Material),
        (Geometry, AssetType.Geometry),
        (Scene, AssetType.Scene),
        (Texture, AssetType.Unknown),
        (Script, AssetType.Script),
        (InputAction, AssetType.InputAction),
        (InputMappingContext, AssetType.InputMappingContext),
        (PhysicsScene, AssetType.PhysicsScene),
        (PhysicsMaterial, AssetType.PhysicsMaterial),
        (CollisionShape, AssetType.CollisionShape),
    ];

    private static readonly AssetFileType[] Types =
    [
        .. new AssetFileType[]
        {
            new(InputActionFile, AssetFileCategory.Input, CookedExtension: null, AssetType.Unknown),
            new(InputFile, AssetFileCategory.Input, CookedExtension: null, AssetType.Unknown),
            new(ImportSettings, AssetFileCategory.ImportSettings, CookedExtension: null, AssetType.Unknown),
            new(LuaScript, AssetFileCategory.Script, Script, AssetType.Script),
            new(LuauScript, AssetFileCategory.Script, Script, AssetType.Script),
            new(Buffer, AssetFileCategory.Cooked, Buffer, AssetType.Unknown),
            new(PhysicsResource, AssetFileCategory.Cooked, PhysicsResource, AssetType.Unknown),
            new(".table", AssetFileCategory.CookedResource, CookedExtension: null, AssetType.Unknown),
            new(".data", AssetFileCategory.CookedResource, CookedExtension: null, AssetType.Unknown),
            new(".gltf", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown),
            new(".glb", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown),
            new(".fbx", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown),
            new(".png", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".jpg", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".jpeg", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".tga", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".bmp", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".hdr", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
        },
        .. CookedDescriptors.Select(static entry => new AssetFileType(entry.Extension + DescriptorSuffix, AssetFileCategory.Descriptor, entry.Extension, entry.Type)),
        .. CookedDescriptors.Select(static entry => new AssetFileType(entry.Extension, AssetFileCategory.Cooked, entry.Extension, entry.Type)),
    ];

    // Longest suffix first, so that ".omat.json" matches as a descriptor rather than as an unknown ".json".
    private static readonly AssetFileType[] BySuffixLength = [.. Types.OrderByDescending(static type => type.Suffix.Length)];

    /// <summary>Gets every file type the pipeline knows.</summary>
    public static IReadOnlyList<AssetFileType> All => Types;

    /// <summary>Finds the type of a file from its name or path.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The file's type, or null when the pipeline does not know it.</returns>
    public static AssetFileType? Find(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return BySuffixLength.FirstOrDefault(type => path.EndsWith(type.Suffix, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>Gets the suffixes of every file type of a category, such as the image formats textures import from.</summary>
    /// <param name="category">The category.</param>
    /// <returns>The suffixes, each with its leading dot.</returns>
    public static IReadOnlyList<string> Suffixes(AssetFileCategory category)
        => [.. Types.Where(type => type.Category == category).Select(static type => type.Suffix)];

    /// <summary>Checks whether a file is of the given category.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <param name="category">The category.</param>
    /// <returns><see langword="true"/> when the file is a known type of that category.</returns>
    public static bool Is(string path, AssetFileCategory category) => Find(path)?.Category == category;

    /// <summary>Removes a file's type suffix, leaving the name an asset is known by.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The path without its type suffix, or the path itself when its type is unknown.</returns>
    public static string RemoveSuffix(string path)
        => Find(path) is { } type ? path[..^type.Suffix.Length] : path;

    /// <summary>Gives the path of the cooked descriptor a file is or cooks into.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The cooked descriptor's path, or null when the file cooks into no descriptor of its own.</returns>
    public static string? ToCookedPath(string path)
        => Find(path) is { CookedExtension: { } extension } type ? path[..^type.Suffix.Length] + extension : null;
}
