// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Materials.Tests.Documents;

/// <summary>Checks material edits: what they change, what they record in the document's history, and what they refuse.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialEditsTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>An edit records one entry and makes the material dirty; undoing it restores the source and the saved state.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnEditIsOneHistoryEntryAndUndoRestoresTheSavedPoint()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Metalness, 0.75f));

        _ = result.Succeeded.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = document.IsDirty.Should().BeTrue();

        _ = materials.Edits.Undo(document).Succeeded.Should().BeTrue();

        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.0f);
        _ = document.IsDirty.Should().BeFalse();

        _ = materials.Edits.Redo(document).Succeeded.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
    }

    /// <summary>A rejected edit changes neither the source nor the history, and is published as a failed operation.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ARejectedEditChangesNothing()
    {
        using var materials = new MaterialDocuments();
        var results = new Mock<IOperationResultPublisher>();
        var edits = new MaterialEdits(materials.Validation, results.Object);
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = MaterialDocuments.ContentOf(document).Material.Source;

        var result = edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Roughness, 4.0f));

        _ = result.Succeeded.Should().BeFalse();
        _ = MaterialDocuments.ContentOf(document).Material.Source.Should().BeSameAs(before);
        _ = document.History.UndoStack.Should().BeEmpty();
        results.Verify(value => value.Publish(It.Is<OperationResult>(published => published.Status == OperationStatus.Failed)), Times.Once);
    }

    /// <summary>A property edit of several descriptors, an enumeration among them, is one history entry.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task APropertyEditOfSeveralDescriptorsIsOneEntry()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder()
            .Set(MaterialDescriptors.Metalness, 0.5f)
            .Set(MaterialDescriptors.Roughness, 0.25f)
            .Set(MaterialDescriptors.AlphaMode, MaterialAlphaMode.Mask)
            .Build();

        _ = materials.Edits.Apply(document, edit).Succeeded.Should().BeTrue();

        var source = MaterialDocuments.ContentOf(document).Material.Source;
        _ = (source.PbrMetallicRoughness.MetallicFactor, source.PbrMetallicRoughness.RoughnessFactor).Should().Be((0.5f, 0.25f));
        _ = source.AlphaMode.Should().Be(MaterialAlphaMode.Mask);
        _ = document.History.UndoStack.Should().ContainSingle();

        _ = materials.Edits.Undo(document).Succeeded.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.AlphaMode.Should().Be(MaterialAlphaMode.Opaque);
    }

    /// <summary>A save of a material that does not match the engine's schema is refused with the reason, and writes nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASaveOfAnInvalidMaterialIsRefused()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var content = MaterialDocuments.ContentOf(document);
        var written = await File.ReadAllBytesAsync(materials.PathOf("Wood"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Metalness, 0.5f));

        // The edits refuse such a source; it reaches the content only from outside them, as a broken history entry would.
        var pbr = content.Material.Source.PbrMetallicRoughness;
        content.Material.Replace(content.Material.Source with
        {
            PbrMetallicRoughness = new(pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA, pbr.MetallicFactor, roughnessFactor: 4.0f),
        });

        var result = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(Oxygen.Editor.Documents.DocumentSaveOutcome.Refused);
        _ = result.Message.Should().NotBeNullOrWhiteSpace();
        _ = (await File.ReadAllBytesAsync(materials.PathOf("Wood"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(written);
    }

    /// <summary>An edit that changes no value records nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnEditThatChangesNothingRecordsNothing()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Metalness, 0.0f)).Succeeded.Should().BeTrue();

        _ = document.History.UndoStack.Should().BeEmpty();
        _ = document.IsDirty.Should().BeFalse();
    }

    /// <summary>A gesture previews its samples without recording, is an edit in progress, and commits as one entry.</summary>
    /// <param name="samples">The number of preview samples.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(1)]
    [DataRow(100)]
    public async Task AGestureCommitsItsSamplesAsOneEntry(int samples)
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = materials.Edits.BeginEditSession(document, "Roughness");

        for (var sample = 1; sample <= samples; sample++)
        {
            _ = materials.Edits.Preview(document, session, PropertyEdit.SingleEdit(MaterialDescriptors.Roughness, sample / (float)samples)).Succeeded.Should().BeTrue();
        }

        _ = document.History.UndoStack.Should().BeEmpty();
        _ = document.Content.HasEditsInProgress.Should().BeTrue();

        materials.Edits.CompleteEditSession(document, session, commit: true);

        _ = document.Content.HasEditsInProgress.Should().BeFalse();
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(1.0f);
    }

    /// <summary>A cancelled gesture restores the source it began with, and Undo during a gesture cancels it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACancelledGestureRestoresTheSourceItBeganWith()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = materials.Edits.BeginEditSession(document, "Roughness");
        _ = materials.Edits.Preview(document, session, PropertyEdit.SingleEdit(MaterialDescriptors.Roughness, 0.9f));

        _ = materials.Edits.CanUndo(document).Should().BeTrue();
        _ = materials.Edits.Undo(document).Succeeded.Should().BeTrue();

        _ = MaterialDocuments.ContentOf(document).Material.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.5f);
        _ = document.History.UndoStack.Should().BeEmpty();
        _ = materials.Edits.Preview(document, session, PropertyEdit.SingleEdit(MaterialDescriptors.Roughness, 0.1f)).Succeeded.Should().BeFalse("the gesture ended");
    }

    /// <summary>Finishing the document's edits, as Save does, commits a gesture in progress.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SavingCommitsTheGestureInProgress()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        var session = materials.Edits.BeginEditSession(document, "Metalness");
        _ = materials.Edits.Preview(document, session, PropertyEdit.SingleEdit(MaterialDescriptors.Metalness, 0.5f));

        _ = (await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = document.History.UndoStack.Should().ContainSingle();
        _ = document.IsDirty.Should().BeFalse();
        var saved = MaterialSourceReader.Read(await File.ReadAllBytesAsync(materials.PathOf("Wood"), this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = saved.PbrMetallicRoughness.MetallicFactor.Should().Be(0.5f);
    }

    /// <summary>Textures are assigned, replaced and cleared per channel; a bad channel or path is refused.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TexturesAreSetPerChannel()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = materials.Edits.SetTexture(document, "base_color", "/Content/Textures/Bark.otex").Succeeded.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureReferences["base_color"].Should().Be("/Content/Textures/Bark.otex");
        _ = materials.Edits.SetTexture(document, "base_color", null).Succeeded.Should().BeTrue();
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureReferences.Should().NotContainKey("base_color");

        _ = materials.Edits.SetTexture(document, "no_such_channel", "/Content/Textures/Bark.otex").Code.Should().Be(MaterialDiagnosticCodes.TextureChannelInvalid);
        _ = materials.Edits.SetTexture(document, "base_color", "Textures/Bark.otex").Code.Should().Be(MaterialDiagnosticCodes.TexturePathInvalid);
        _ = document.History.UndoStack.Should().HaveCount(2);
    }

    /// <summary>Undoing to a source captured before a relocation follows its texture to the texture's current path.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoFollowsATextureThatMovedSinceTheEdit()
    {
        using var materials = new MaterialDocuments();
        var redirects = new Mock<IAssetRedirects>();
        _ = redirects.Setup(value => value.Resolve(It.IsAny<string>())).Returns((string path) => path.Replace("/Bark.", "/Oak.", StringComparison.Ordinal));
        var edits = new MaterialEdits(materials.Validation, redirects: redirects.Object);
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = edits.SetTexture(document, "base_color", "/Content/Textures/Bark.otex");
        _ = edits.SetTexture(document, "base_color", "/Content/Textures/Stone.otex");

        _ = edits.Undo(document).Succeeded.Should().BeTrue();

        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureReferences["base_color"].Should().Be("/Content/Textures/Oak.otex");
    }
}
