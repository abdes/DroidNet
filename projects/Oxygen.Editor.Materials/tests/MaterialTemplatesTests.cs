// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.Materials.Tests;

/// <summary>A new material is a source the material reader reads: white, opaque and single-sided, named after its file.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed class MaterialTemplatesTests
{
    [TestMethod]
    public void NewMaterialReadsAsAnOpaqueSingleSidedSourceNamedAfterItsFile()
    {
        var source = MaterialSourceReader.Read(Encoding.UTF8.GetBytes(MaterialTemplates.New("Wood")));

        _ = source.Name.Should().Be("Wood");
        _ = source.AlphaMode.Should().Be(MaterialAlphaMode.Opaque);
        _ = source.DoubleSided.Should().BeFalse();
    }

    [TestMethod]
    public void TemplateCreatesMaterialDescriptorsInTheMaterialsFolder()
    {
        _ = MaterialTemplates.Asset.Folder.Should().Be("Materials");
        _ = MaterialTemplates.Asset.Suffixes.Should().Equal(AssetFileTypes.MaterialDescriptor);
    }
}
