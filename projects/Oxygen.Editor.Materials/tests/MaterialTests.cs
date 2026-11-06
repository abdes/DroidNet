// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Tests;

/// <summary>Checks the material model: replacing its source, and the rules of texture references.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialTests
{
    private static AssetPath Identity { get; } = AssetPath.Parse("/Content/Materials/Oak.omat");

    private static MaterialSource Oak { get; } = MaterialSourceReader.Read(
        """{"name":"Oak","textures":{"base_color":{"virtual_path":"../Textures/Oak.otex"},"normal":{"virtual_path":"../Textures/OakNormal.otex"}}}"""u8, Identity);

    [TestMethod]
    public void Replace_ADifferentSource_RaisesChangedOnce()
    {
        var material = new Material(Oak, Identity);
        var changes = 0;
        material.Changed += (_, _) => changes++;
        var edited = Oak with { DoubleSided = true };

        material.Replace(edited);
        material.Replace(edited);

        _ = material.Source.Should().BeSameAs(edited);
        _ = changes.Should().Be(1);
    }

    [TestMethod]
    public void Retarget_MovesOnlyTheMappedTextures()
    {
        var retargeted = Material.Retarget(Oak, texture => texture.Name == "Oak.otex" ? AssetPath.Parse("/Content/Wood/Oak.otex") : null);

        _ = retargeted.TextureReferences.Should().Equal(new Dictionary<string, AssetPath>(StringComparer.Ordinal)
        {
            ["base_color"] = AssetPath.Parse("/Content/Wood/Oak.otex"),
            ["normal"] = AssetPath.Parse("/Content/Textures/OakNormal.otex"),
        });
    }

    [TestMethod]
    public void Retarget_NothingMoved_ReturnsTheSameSource()
        => _ = Material.Retarget(Oak, texture => texture).Should().BeSameAs(Oak);

    [TestMethod]
    public void AreSame_ComparesWhatSavingWrites()
    {
        var reread = MaterialSourceReader.Read(Material.Serialize(Oak, Identity), Identity);

        _ = new Material(Oak, Identity).IsSameAs(reread).Should().BeTrue();
        _ = new Material(Oak, Identity).IsSameAs(Oak with { AlphaCutoff = 0.25f }).Should().BeFalse();
    }

    [TestMethod]
    [DataRow("base_color", null)]
    [DataRow("no_such_channel", MaterialDiagnosticCodes.TextureChannelInvalid)]
    [DataRow(null, MaterialDiagnosticCodes.TextureChannelInvalid)]
    public void CheckTexture_ReturnsTheCodeOfTheProblem(string? channel, string? code)
        => _ = Material.CheckTexture(channel).Should().Be(code);
}
