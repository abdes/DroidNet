// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

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
    public const string DescriptorSuffix = AssetPath.AuthoringSuffix;

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

    /// <summary>The suffix of an input source, a container of actions and mapping contexts.</summary>
    public const string InputFile = ".input.json";

    /// <summary>The suffix of the import settings stored beside a model.</summary>
    public const string ImportSettings = ".import.json";

    private static readonly (string Extension, AssetType Type)[] CookedAssets =
    [
        (Material, AssetType.Material),
        (Geometry, AssetType.Geometry),
        (Scene, AssetType.Scene),
        (Script, AssetType.Script),
        (InputAction, AssetType.InputAction),
        (InputMappingContext, AssetType.InputMappingContext),
        (PhysicsScene, AssetType.PhysicsScene),
        (PhysicsMaterial, AssetType.PhysicsMaterial),
        (CollisionShape, AssetType.CollisionShape),
    ];

    private static readonly string[] ModelProducts = [Geometry, Material, Texture, Scene];

    private static readonly AssetFileType[] Types =
    [
        .. new AssetFileType[]
        {
            new(InputFile, AssetFileCategory.Input, CookedExtension: null, AssetType.Unknown) { ProductExtensions = [InputAction, InputMappingContext] },
            new(ImportSettings, AssetFileCategory.ImportSettings, CookedExtension: null, AssetType.Unknown),
            new(LuaScript, AssetFileCategory.Script, Script, AssetType.Script),
            new(LuauScript, AssetFileCategory.Script, Script, AssetType.Script),
            new(TextureDescriptor, AssetFileCategory.Descriptor, Texture, AssetType.Unknown) { CookedFileKind = FileKind.TextureDescriptor },
            new(Texture, AssetFileCategory.Cooked, Texture, AssetType.Unknown) { CookedFileKind = FileKind.TextureDescriptor },
            new(Buffer, AssetFileCategory.Cooked, Buffer, AssetType.Unknown),
            new(PhysicsResource, AssetFileCategory.Cooked, PhysicsResource, AssetType.Unknown),
            new(".table", AssetFileCategory.CookedResource, CookedExtension: null, AssetType.Unknown),
            new(".data", AssetFileCategory.CookedResource, CookedExtension: null, AssetType.Unknown),
            new(".gltf", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown) { ProductExtensions = ModelProducts },
            new(".glb", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown) { ProductExtensions = ModelProducts },
            new(".fbx", AssetFileCategory.Model, CookedExtension: null, AssetType.Unknown) { ProductExtensions = ModelProducts },
            new(".png", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".jpg", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".jpeg", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".tga", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".bmp", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
            new(".hdr", AssetFileCategory.Image, CookedExtension: null, AssetType.Unknown),
        },
        .. CookedAssets.Select(static entry => new AssetFileType(entry.Extension + DescriptorSuffix, AssetFileCategory.Descriptor, entry.Extension, entry.Type)),
        .. CookedAssets.Select(static entry => new AssetFileType(entry.Extension, AssetFileCategory.Cooked, entry.Extension, entry.Type)),
    ];

    // Longest suffix first, so that ".omat.json" matches as a descriptor rather than as an unknown ".json".
    private static readonly AssetFileType[] BySuffixLength = [.. Types.OrderByDescending(static type => type.Suffix.Length)];

    /// <summary>Gets every file type the pipeline knows.</summary>
    public static IReadOnlyList<AssetFileType> All => Types;

    /// <summary>
    /// Finds the type of a file from its name or path. Oxygen's suffixes match exactly, as the engine hashes the
    /// identity they end; model and image formats match in any case, as the cooker detects them.
    /// </summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The file's type, or null when the pipeline does not know it.</returns>
    public static AssetFileType? Find(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return BySuffixLength.FirstOrDefault(type => type.Matches(path));
    }

    /// <summary>
    /// Gets a file name's suffix: its known type's suffix as the name spells it, keeping compound suffixes such as
    /// <c>.omat.json</c> and <c>.input.json</c> whole, and otherwise its last extension.
    /// </summary>
    /// <param name="fileName">The file name.</param>
    /// <returns>The suffix, such as <c>.omat.json</c> or <c>.png</c>; empty for a name without one.</returns>
    public static string GetSuffix(string fileName)
    {
        ArgumentNullException.ThrowIfNull(fileName);
        return Find(fileName) is { } type && fileName.Length > type.Suffix.Length ? fileName[^type.Suffix.Length..] : Path.GetExtension(fileName);
    }

    /// <summary>Checks whether a file is a cooked asset that containers of a category produce, such as a model's geometry.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <param name="container">The category of the containers, such as <see cref="AssetFileCategory.Model"/>.</param>
    /// <returns><see langword="true"/> when the file is a cooked descriptor of a type those containers produce.</returns>
    public static bool IsProductOf(string path, AssetFileCategory container)
        => Find(path) is { Category: AssetFileCategory.Cooked, CookedExtension: { } extension }
            && Types.Any(type => type.Category == container && type.ProductExtensions.Contains(extension, StringComparer.Ordinal));

    /// <summary>
    /// Finds the container a product is named under: the input source <c>/Content/Input/Player.input</c> of
    /// <c>/Content/Input/Player.input/Jump.oiact</c>, or the model <c>/Content/Ship/Ship.glb</c> of
    /// <c>/Content/Ship/Ship.glb/Paint.omat</c>.
    /// </summary>
    /// <param name="product">The product's identity.</param>
    /// <returns>The container's identity, or <see langword="null"/> when the path is not named under a container that
    /// produces its type.</returns>
    public static AssetPath? ContainerOf(AssetPath product)
    {
        ArgumentNullException.ThrowIfNull(product);
        return product.Parent is { IsMount: false } container
            && Find(product.Name) is { Category: AssetFileCategory.Cooked, CookedExtension: { } extension }
            && ContainerType(container) is { } type
            && type.ProductExtensions.Contains(extension, StringComparer.Ordinal)
                ? container
                : null;
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

    /// <summary>
    /// Gets the identity of the asset a source file stands for: the cooked descriptor it is or cooks into
    /// (<c>Red.omat.json</c> and <c>Red.omat</c> are <c>Red.omat</c>, <c>Orbit.lua</c> is <c>Orbit.oscript</c>), and
    /// otherwise the file without its authoring suffix (<c>Player.input.json</c> is the container
    /// <c>Player.input</c>). Import settings stand for no asset and keep their path.
    /// </summary>
    /// <param name="source">The source file's path.</param>
    /// <returns>The identity.</returns>
    public static AssetPath ToIdentity(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return Find(source.Name) switch
        {
            { Category: AssetFileCategory.ImportSettings } => source,
            { CookedExtension: { } extension } type => source.WithName(source.Name[..^type.Suffix.Length] + extension),
            _ => source.WithoutAuthoringSuffix(),
        };
    }

    /// <summary>
    /// Gets the files that can be the source of an asset, most specific first: the authored descriptor or scripts that
    /// cook into it (<c>Red.omat.json</c> for <c>Red.omat</c>; <c>Orbit.lua</c> and <c>Orbit.luau</c> for
    /// <c>Orbit.oscript</c>), the file with the authoring suffix (<c>Player.input.json</c> for the container
    /// <c>Player.input</c>), and the file itself (<c>Ship.glb</c>).
    /// </summary>
    /// <param name="identity">The asset's identity.</param>
    /// <returns>The candidate source paths; <see cref="ToIdentity"/> maps each back to <paramref name="identity"/>.</returns>
    public static IReadOnlyList<AssetPath> SourcesOf(AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        if (identity.IsMount)
        {
            return [identity];
        }

        var sources = new List<AssetPath>();
        if (Find(identity.Name) is { Category: AssetFileCategory.Cooked, CookedExtension: { } extension })
        {
            var stem = identity.Name[..^extension.Length];
            sources.AddRange(Types.Where(type => type.Category is AssetFileCategory.Descriptor or AssetFileCategory.Script
                    && string.Equals(type.CookedExtension, extension, StringComparison.Ordinal))
                .Select(type => identity.WithName(stem + type.Suffix)));
        }

        if (Find(identity.WithAuthoringSuffix().Name) is not null)
        {
            sources.Add(identity.WithAuthoringSuffix());
        }

        sources.Add(identity);
        return [.. sources.Distinct().Where(source => ToIdentity(source) == identity)];
    }

    /// <summary>Gives the path of the cooked descriptor a file is or cooks into.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The cooked descriptor's path, or null when the file cooks into no descriptor of its own.</returns>
    public static string? ToCookedPath(string path)
        => Find(path) is { CookedExtension: { } extension } type ? path[..^type.Suffix.Length] + extension : null;

    // A model is identified by its source file (Ship.glb); an input source by its file without the authoring
    // suffix (Player.input for Player.input.json).
    private static AssetFileType? ContainerType(AssetPath container)
        => Find(container.Name) is { Category: AssetFileCategory.Model } model
            ? model
            : Find(container.WithAuthoringSuffix().Name) is { Category: AssetFileCategory.Input } input ? input : null;
}
