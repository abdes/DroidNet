// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.Materials.Tests;

/// <summary>Checks the material model: replacing its source, and the rules of texture references.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialTests
{
    private static MaterialSource Oak { get; } = MaterialSourceReader.Read(
        """{"name":"Oak","textures":{"base_color":{"virtual_path":"/Content/Textures/Oak.otex"},"normal":{"virtual_path":"/Content/Textures/OakNormal.otex"}}}"""u8);

    [TestMethod]
    public void Replace_ADifferentSource_RaisesChangedOnce()
    {
        var material = new Material(Oak);
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
        var retargeted = Material.Retarget(Oak, path => path.EndsWith("/Oak.otex", StringComparison.Ordinal) ? "/Content/Wood/Oak.otex" : null);

        _ = retargeted.TextureReferences.Should().Equal(new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["base_color"] = "/Content/Wood/Oak.otex",
            ["normal"] = "/Content/Textures/OakNormal.otex",
        });
    }

    [TestMethod]
    public void Retarget_NothingMoved_ReturnsTheSameSource()
        => _ = Material.Retarget(Oak, path => path).Should().BeSameAs(Oak);

    [TestMethod]
    public void AreSame_ComparesWhatSavingWrites()
    {
        var reread = MaterialSourceReader.Read(Material.Serialize(Oak));

        _ = Material.AreSame(Oak, reread).Should().BeTrue();
        _ = Material.AreSame(Oak, Oak with { AlphaCutoff = 0.25f }).Should().BeFalse();
    }

    [TestMethod]
    [DataRow("base_color", "/Content/Textures/Bark.otex", null)]
    [DataRow("base_color", null, null)]
    [DataRow("no_such_channel", "/Content/Textures/Bark.otex", MaterialDiagnosticCodes.TextureChannelInvalid)]
    [DataRow(null, null, MaterialDiagnosticCodes.TextureChannelInvalid)]
    [DataRow("base_color", "Textures/Bark.otex", MaterialDiagnosticCodes.TexturePathInvalid)]
    [DataRow("base_color", "asset:///Content/Textures/Bark.otex", MaterialDiagnosticCodes.TexturePathInvalid)]
    public void CheckTexture_ReturnsTheCodeOfTheProblem(string? channel, string? virtualPath, string? code)
        => _ = Material.CheckTexture(channel, virtualPath).Should().Be(code);
}
