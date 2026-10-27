// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>Checks the pipeline's file types and what each cooks into.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class AssetFileTypesTests
{
    [TestMethod]
    [DataRow("/Content/Materials/Red.omat.json", AssetFileCategory.Descriptor, "/Content/Materials/Red.omat", AssetType.Material)]
    [DataRow("/Content/Textures/Sky.otex.json", AssetFileCategory.Descriptor, "/Content/Textures/Sky.otex", AssetType.Unknown)]
    [DataRow("/Content/Scenes/Level.oscene", AssetFileCategory.Cooked, "/Content/Scenes/Level.oscene", AssetType.Scene)]
    [DataRow("/Content/Scripts/Door.lua", AssetFileCategory.Script, "/Content/Scripts/Door.oscript", AssetType.Script)]
    [DataRow("/Content/Scripts/Door.luau", AssetFileCategory.Script, "/Content/Scripts/Door.oscript", AssetType.Script)]
    [DataRow("/Content/Input/Player.input.json", AssetFileCategory.Input, null, AssetType.Unknown)]
    [DataRow("C:/Models/Crate.gltf.import.json", AssetFileCategory.ImportSettings, null, AssetType.Unknown)]
    [DataRow("/Content/Models/Crate.glb.preview.json", AssetFileCategory.ModelPreview, null, AssetType.Unknown)]
    [DataRow("C:/Models/Crate.FBX", AssetFileCategory.Model, null, AssetType.Unknown)]
    [DataRow("C:/Images/Sky.hdr", AssetFileCategory.Image, null, AssetType.Unknown)]
    [DataRow("textures.table", AssetFileCategory.CookedResource, null, AssetType.Unknown)]
    public void FileTypeNamesWhatAFileIsAndWhatItCooksInto(string path, AssetFileCategory category, string? cooked, AssetType assetType)
    {
        var type = AssetFileTypes.Find(path);

        _ = type.Should().NotBeNull();
        _ = type!.Category.Should().Be(category);
        _ = type.AssetType.Should().Be(assetType);
        _ = AssetFileTypes.ToCookedPath(path).Should().Be(cooked);
    }

    [TestMethod]
    [DataRow("Red.omat.json", "Red")]
    [DataRow("Player.input.json", "Player")]
    [DataRow("Door.luau", "Door")]
    [DataRow("notes.txt", "notes.txt")]
    public void RemovingTheSuffixLeavesTheAssetName(string fileName, string expected)
        => _ = AssetFileTypes.RemoveSuffix(fileName).Should().Be(expected);

    [TestMethod]
    public void UnknownFilesHaveNoType()
    {
        _ = AssetFileTypes.Find("readme.md").Should().BeNull();
        _ = AssetFileTypes.Find("settings.json").Should().BeNull();
        _ = AssetFileTypes.ToCookedPath("settings.json").Should().BeNull();
    }

    [TestMethod]
    [DataRow("/Content/Input/Player.input.json", new[] { ".oiact", ".oimap" })]
    [DataRow("C:/Models/Ship.glb", new[] { ".ogeo", ".omat", ".otex", ".oscene" })]
    public void ContainerNamesTheAssetTypesItProduces(string path, string[] products)
    {
        var type = AssetFileTypes.Find(path)!;

        _ = type.ProductExtensions.Should().BeEquivalentTo(products);
        _ = AssetFileTypes.ToCookedPath(path).Should().BeNull();
    }

    [TestMethod]
    [DataRow("/Content/Input/Player.input/Jump.oiact", "/Content/Input/Player.input")]
    [DataRow("/Content/Input/Player.input/OnFoot.oimap", "/Content/Input/Player.input")]
    [DataRow("/Content/Input/Jump.oiact", null)]
    [DataRow("/Content/Input/Player.input/Red.omat", null)]
    [DataRow("/Content/Materials/Red.omat", null)]
    [DataRow("/Content/Input/Player.input", null)]
    public void ProductIsNamedUnderItsContainer(string product, string? container)
        => _ = (AssetFileTypes.ContainerOf(AssetPath.Parse(product))?.VirtualPath).Should().Be(container);

    [TestMethod]
    [DataRow("/Content/Vehicles/Car.glb", "/Content/Vehicles/Car.glb/Car.oscene")]
    [DataRow("/Content/Props/Crate.fbx", "/Content/Props/Crate.fbx/Crate.oscene")]
    [DataRow("/Content/Prefabs/Watchtower.oscene", "/Content/Prefabs/Watchtower.oscene")]
    [DataRow("/Content/Textures/Brick.png", null)]
    [DataRow("/Content/Materials/Red.omat", null)]
    public void SourceInstancesAsItsSceneProduct(string source, string? scene)
        => _ = (AssetFileTypes.SceneProductOf(AssetPath.Parse(source))?.VirtualPath).Should().Be(scene);

    [TestMethod]
    [DataRow("/Content/Materials/Red.omat.json")]
    [DataRow("/Content/Scripts/Door.lua")]
    [DataRow("/Content/Textures/Sky.otex.json")]
    public void SingleAssetSourceIsNoContainer(string path)
        => _ = AssetFileTypes.Find(path)!.ProductExtensions.Should().BeEmpty();

    [TestMethod]
    [DataRow("/Content/Textures/Sky.otex.json")]
    [DataRow("/Content/Textures/Sky.otex")]
    public void TextureDescriptorHasItsCookedFileKind(string path)
        => _ = AssetFileTypes.Find(path)!.CookedFileKind.Should().Be(Persistence.LooseCooked.FileKind.TextureDescriptor);

    [TestMethod]
    [DataRow("/Content/Materials/Red.OMAT.json")]
    [DataRow("/Content/Scenes/Level.OSCENE")]
    [DataRow("/Content/Input/Player.INPUT.json")]
    public void OxygenSuffixesMatchExactly(string path)
        => _ = AssetFileTypes.Find(path).Should().BeNull();

    [TestMethod]
    [DataRow("C:/Models/Crate.FBX", AssetFileCategory.Model)]
    [DataRow("C:/Images/Sky.PNG", AssetFileCategory.Image)]
    public void ForeignFormatsMatchInAnyCase(string path, AssetFileCategory category)
        => _ = AssetFileTypes.Find(path)!.Category.Should().Be(category);

    [TestMethod]
    [DataRow("Red.omat.json", ".omat.json")]
    [DataRow("Player.input.json", ".input.json")]
    [DataRow("Crate.glb.import.json", ".import.json")]
    [DataRow("Sky.PNG", ".PNG")]
    [DataRow("notes.txt", ".txt")]
    [DataRow("Folder", "")]
    public void SuffixKeepsCompoundSuffixesWhole(string fileName, string expected)
        => _ = AssetFileTypes.GetSuffix(fileName).Should().Be(expected);

    [TestMethod]
    [DataRow("/Content/Models/Hull.ogeo", true)]
    [DataRow("/Content/Models/Paint.otex", true)]
    [DataRow("/Content/Models/Paint.omat.json", false)]
    [DataRow("/Content/Input/Jump.oiact", false)]
    public void ModelProductsAreTheCookedTypesModelsProduce(string path, bool expected)
        => _ = AssetFileTypes.IsProductOf(path, AssetFileCategory.Model).Should().Be(expected);

    [TestMethod]
    public void EverySuffixNamesOneType()
        => _ = AssetFileTypes.All.Select(static type => type.Suffix.ToUpperInvariant()).Should().OnlyHaveUniqueItems();

    [TestMethod]
    public void AssetTypeValuesMatchTheCookedFormat()
    {
        _ = ((byte)AssetType.Material).Should().Be(1);
        _ = ((byte)AssetType.InputMappingContext).Should().Be(6);
        _ = ((byte)AssetType.PhysicsScene).Should().Be(9);
    }

    [TestMethod]
    public void ModelPreview_IsBesideTheModelAndStandsForNoAsset()
    {
        var preview = AssetFileTypes.PreviewOf("/Content/Models/Crate.glb");

        _ = preview.Should().Be("/Content/Models/Crate.glb" + AssetFileTypes.ModelPreview, "it keeps the model's whole name, so Crate.glb and Crate.fbx each have their own");
        _ = AssetFileTypes.ToIdentity(AssetPath.Parse(preview)).Should().Be(AssetPath.Parse(preview), "a preview is no asset of its own");
    }
}
