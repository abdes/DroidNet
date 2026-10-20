// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Tests.TestSupport;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Tests.Documents;

/// <summary>Checks the material kind: how a material file opens, saves a copy and follows a relocation.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialDocumentKindTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A material opens named after its file, whatever name its descriptor holds.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AMaterialIsNamedAfterItsFile()
    {
        using var materials = new MaterialDocuments();
        await materials.FileSystem.File.WriteAllTextAsync(MaterialDocuments.PathOf("Wood"), MaterialTemplates.New("Old name"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var document = await materials.OpenAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = document.Title.Should().Be("Wood");
        _ = MaterialDocuments.ContentOf(document).Material.Source.Name.Should().Be("Wood");
    }

    /// <summary>A file that is not a material fails the open with the reason.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AFileThatIsNotAMaterialDoesNotOpen()
    {
        using var materials = new MaterialDocuments();
        await materials.FileSystem.File.WriteAllTextAsync(MaterialDocuments.PathOf("Broken"), "{ not json", this.TestContext.CancellationToken).ConfigureAwait(false);

        var opened = await materials.Documents.OpenAsync(MaterialDocuments.FileOf("Broken"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = opened.Document.Should().BeNull();
        _ = opened.Problem.Should().Contain("is not a valid material");
    }

    /// <summary>A copy is named after its own file; the original keeps its name and its unsaved edits.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACopyIsNamedAfterItsFile()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", AssetPath.Parse("/Content/Textures/Bark.otex"));

        var copy = MaterialDocuments.FileOf("Wood Copy");
        _ = (await document.SaveCopyAsync(copy, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        var written = MaterialSourceReader.Read(await materials.FileSystem.File.ReadAllBytesAsync(copy.Path.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = written.Name.Should().Be("Wood Copy");
        _ = written.TextureReferences["base_color"].Should().Be(AssetPath.Parse("/Content/Textures/Bark.otex"));
        _ = document.IsDirty.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.Name.Should().Be("Wood");
    }

    /// <summary>A relocation renames the material and moves its texture references, without a history entry.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ARelocationRenamesAndRetargetsTheMaterial()
    {
        using var materials = new MaterialDocuments();
        await materials.FileSystem.File.WriteAllTextAsync(MaterialDocuments.PathOf("Wood"), MaterialTemplates.New("Wood"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var document = await materials.OpenAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", AssetPath.Parse("/Content/Textures/Bark.otex"));
        _ = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var entries = document.History.UndoStack.Count;
        materials.FileSystem.File.Move(MaterialDocuments.PathOf("Wood"), MaterialDocuments.PathOf("Oak"));

        await document.FollowRelocationAsync(
            new DocumentRelocation(MaterialDocuments.FileOf("Oak"), document.Version, static texture => AssetPath.Parse(texture.VirtualPath.Replace("/Bark.", "/Oak.", StringComparison.Ordinal))),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var source = MaterialDocuments.ContentOf(document).Material.Source;
        _ = document.Title.Should().Be("Oak");
        _ = source.Name.Should().Be("Oak");
        _ = source.TextureReferences["base_color"].Should().Be(AssetPath.Parse("/Content/Textures/Oak.otex"));
        _ = document.History.UndoStack.Should().HaveCount(entries);
        _ = document.IsDirty.Should().BeFalse();
    }
}
