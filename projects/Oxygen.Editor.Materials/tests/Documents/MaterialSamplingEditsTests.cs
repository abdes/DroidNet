// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Materials.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Tests.Documents;

/// <summary>Checks edits of how a channel reads its texture, of the occlusion mode and of the procedural grid.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialSamplingEditsTests
{
    private static readonly AssetPath Bark = AssetPath.Parse("/Content/Textures/Bark.otex");
    private static readonly AssetPath Moss = AssetPath.Parse("/Content/Textures/Moss.otex");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Every field of a slot's sampling survives a save and a reload.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task EverySlotFieldSurvivesSaveAndReload()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "normal", Bark);
        var slot = MaterialDescriptors.Slot("normal");

        var edit = new PropertyEdit.Builder()
            .Set(slot.UvSet, 1)
            .Set(slot.ScaleU, 4.0f)
            .Set(slot.ScaleV, 2.0f)
            .Set(slot.OffsetU, 0.25f)
            .Set(slot.OffsetV, 0.5f)
            .Set(slot.RotationRadians, 1.5f)
            .Set(slot.AddressU, MaterialTextureAddress.Clamp)
            .Set(slot.AddressV, MaterialTextureAddress.Mirror)
            .Set(slot.MagFilter, MaterialTextureFilter.Nearest)
            .Set(slot.MinFilter, MaterialTextureFilter.Nearest)
            .Set(slot.MipFilter, MaterialTextureMipFilter.None)
            .Build();
        _ = materials.Edits.Apply(document, edit).Succeeded.Should().BeTrue();

        var reloaded = await SaveAndReadAsync(materials, document, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reloaded.TextureBindings["normal"].Should().Be(new MaterialTextureBinding(Bark)
        {
            UvSet = 1,
            UvTransform = new MaterialUvTransform(new Vector2(4, 2), new Vector2(0.25f, 0.5f), 1.5f),
            Sampler = new MaterialTextureSampler
            {
                AddressU = MaterialTextureAddress.Clamp,
                AddressV = MaterialTextureAddress.Mirror,
                MagFilter = MaterialTextureFilter.Nearest,
                MinFilter = MaterialTextureFilter.Nearest,
                MipFilter = MaterialTextureMipFilter.None,
            },
        });
    }

    /// <summary>A slot field put back to its default is no longer written.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASlotFieldAtItsDefaultIsNotWritten()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", Bark);
        var slot = MaterialDescriptors.Slot("base_color");
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(slot.ScaleU, 4.0f));

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(slot.ScaleU, 1.0f));

        var written = MaterialSourceWriter.ToJson(MaterialDocuments.ContentOf(document).Material.Source, MaterialDocuments.IdentityOf("Wood"));
        _ = written["textures"]!["base_color"]!.AsObject().Select(static field => field.Key).Should().Equal("virtual_path");
    }

    /// <summary>A UV set a vertex does not hold is refused.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AUvSetAVertexDoesNotHoldIsRefused()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", Bark);

        var result = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Slot("base_color").UvSet, 2));

        _ = result.Code.Should().Be(MaterialDiagnosticCodes.PropertyValueOutOfRange);
    }

    /// <summary>Changing a slot's texture keeps its sampling; clearing it drops the sampling, and undo brings it back.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SamplingFollowsTheBinding()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", Bark);
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Slot("base_color").UvSet, 1));

        _ = materials.Edits.SetTexture(document, "base_color", Moss);
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["base_color"].UvSet.Should().Be(1);

        _ = materials.Edits.SetTexture(document, "base_color", texture: null);
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings.Should().NotContainKey("base_color");

        _ = materials.Edits.Undo(document);
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["base_color"].Should().Be(new MaterialTextureBinding(Moss) { UvSet = 1 });
    }

    /// <summary>Applying to all copies the sampling to the assigned slots only, keeps their textures, and is one history entry.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ApplyToAllCopiesToAssignedSlotsAsOneEntry()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", Bark);
        _ = materials.Edits.SetTexture(document, "roughness", Moss);
        var slot = MaterialDescriptors.Slot("base_color");
        _ = materials.Edits.Apply(document, new PropertyEdit.Builder().Set(slot.ScaleU, 4.0f).Set(slot.AddressU, MaterialTextureAddress.Clamp).Build());
        var entries = document.History.UndoStack.Count;

        _ = materials.Edits.ApplySamplingToAll(document, "base_color").Succeeded.Should().BeTrue();

        _ = document.History.UndoStack.Should().HaveCount(entries + 1);
        var bindings = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings;
        _ = bindings.Keys.Should().BeEquivalentTo("base_color", "roughness");
        _ = bindings["roughness"].Should().Be(bindings["base_color"] with { Texture = Moss });

        _ = materials.Edits.Undo(document);
        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["roughness"].Should().Be(new MaterialTextureBinding(Moss));
    }

    /// <summary>Resetting a slot's sampling keeps its texture and writes nothing else.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ResetSamplingKeepsTheTexture()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetTexture(document, "base_color", Bark);
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.Slot("base_color").UvSet, 1));

        _ = materials.Edits.ResetSampling(document, "base_color").Succeeded.Should().BeTrue();

        _ = MaterialDocuments.ContentOf(document).Material.Source.TextureBindings["base_color"].Should().Be(new MaterialTextureBinding(Bark));
    }

    /// <summary>The occlusion mode survives a save and a reload, and its default is not written.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheOcclusionModeRoundTrips()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.AmbientOcclusionMode, MaterialAmbientOcclusionMode.Strength));
        var reloaded = await SaveAndReadAsync(materials, document, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reloaded.AmbientOcclusionMode.Should().Be(MaterialAmbientOcclusionMode.Strength);

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialDescriptors.AmbientOcclusionMode, MaterialAmbientOcclusionMode.Factor));
        var written = MaterialSourceWriter.ToJson(MaterialDocuments.ContentOf(document).Material.Source, MaterialDocuments.IdentityOf("Wood"));
        _ = written.ContainsKey("ambient_occlusion_mode").Should().BeFalse();
    }

    /// <summary>The grid's presence is its toggle: adding it states nothing, and removing it can be undone with its values.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheGridsPresenceIsItsToggle()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = materials.Edits.SetProceduralGrid(document, new MaterialProceduralGrid()).Succeeded.Should().BeTrue();
        var written = MaterialSourceWriter.ToJson(MaterialDocuments.ContentOf(document).Material.Source, MaterialDocuments.IdentityOf("Wood"));
        _ = written["procedural_grid"]!.AsObject().Should().BeEmpty();

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialGridProperties.FadeEnd, 60.0f));
        _ = materials.Edits.SetProceduralGrid(document, grid: null);
        _ = MaterialDocuments.ContentOf(document).Material.Source.ProceduralGrid.Should().BeNull();

        _ = materials.Edits.Undo(document);
        _ = MaterialDocuments.ContentOf(document).Material.Source.ProceduralGrid!.FadeEnd.Should().Be(60.0f);
    }

    /// <summary>Every grid field survives a save and a reload; one at the engine's default is not written.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task EveryGridFieldRoundTripsAndItsDefaultIsNotWritten()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetProceduralGrid(document, new MaterialProceduralGrid());
        var builder = new PropertyEdit.Builder()
            .Set(MaterialGridProperties.SpacingX, 0.5f)
            .Set(MaterialGridProperties.SpacingY, 2.0f)
            .Set(MaterialGridProperties.MajorEvery, 5U)
            .Set(MaterialGridProperties.LineThickness, 0.01f)
            .Set(MaterialGridProperties.MajorThickness, 0.03f)
            .Set(MaterialGridProperties.AxisThickness, 0.05f)
            .Set(MaterialGridProperties.FadeStart, 20.0f)
            .Set(MaterialGridProperties.FadeEnd, 60.0f);
        foreach (var color in new[] { MaterialGridProperties.MinorColor, MaterialGridProperties.MajorColor, MaterialGridProperties.AxisColorX, MaterialGridProperties.AxisColorY, MaterialGridProperties.OriginColor })
        {
            _ = builder.Set(color.Channels[1], 0.125f);
        }

        _ = materials.Edits.Apply(document, builder.Build()).Succeeded.Should().BeTrue();

        var grid = (await SaveAndReadAsync(materials, document, this.TestContext.CancellationToken).ConfigureAwait(false)).ProceduralGrid!;
        _ = grid.Spacing.Should().Be(new Vector2(0.5f, 2.0f));
        _ = grid.MajorEvery.Should().Be(5U);
        _ = (grid.LineThickness, grid.MajorThickness, grid.AxisThickness).Should().Be((0.01f, 0.03f, 0.05f));
        _ = (grid.FadeStart, grid.FadeEnd).Should().Be((20.0f, 60.0f));
        _ = grid.MinorColor!.Value.Y.Should().Be(0.125f);
        _ = grid.OriginColor!.Value.Y.Should().Be(0.125f);

        var defaults = MaterialProceduralGrid.EngineDefaults;
        _ = materials.Edits.Apply(document, new PropertyEdit.Builder()
            .Set(MaterialGridProperties.SpacingX, 1.0f)
            .Set(MaterialGridProperties.SpacingY, 1.0f)
            .Set(MaterialGridProperties.LineThickness, defaults.LineThickness!.Value)
            .Build());
        var written = (JsonObject)MaterialSourceWriter.ToJson(MaterialDocuments.ContentOf(document).Material.Source, MaterialDocuments.IdentityOf("Wood"))["procedural_grid"]!;
        _ = written.ContainsKey("spacing").Should().BeFalse();
        _ = written.ContainsKey("line_thickness").Should().BeFalse();
    }

    /// <summary>What the cooker would refuse is refused as it is edited.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AGridTheCookerRefusesIsRefused()
    {
        using var materials = new MaterialDocuments();
        var document = await materials.CreateAsync("Wood", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materials.Edits.SetProceduralGrid(document, new MaterialProceduralGrid());

        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialGridProperties.SpacingX, 0.0f)).Code.Should().Be(MaterialDiagnosticCodes.PropertyValueOutOfRange);
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialGridProperties.MajorEvery, 0U)).Code.Should().Be(MaterialDiagnosticCodes.PropertyValueOutOfRange);
        _ = materials.Edits.Apply(document, PropertyEdit.SingleEdit(MaterialGridProperties.FadeStart, -1.0f)).Code.Should().Be(MaterialDiagnosticCodes.PropertyValueOutOfRange);
        _ = materials.Edits.Apply(document, new PropertyEdit.Builder().Set(MaterialGridProperties.FadeStart, 20.0f).Set(MaterialGridProperties.FadeEnd, 10.0f).Build()).Code.Should().Be(MaterialDiagnosticCodes.EditRejected);
    }

    private static async Task<MaterialSource> SaveAndReadAsync(MaterialDocuments materials, Document document, CancellationToken cancellationToken)
    {
        _ = (await document.SaveAsync(cancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var bytes = await materials.FileSystem.File.ReadAllBytesAsync(MaterialDocuments.PathOf("Wood"), cancellationToken).ConfigureAwait(false);
        return MaterialSourceReader.Read(bytes, MaterialDocuments.IdentityOf("Wood"));
    }
}
