// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks mandatory inventory coverage and editor/SDK path resolution.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes.")]
public sealed class EditorArtifactInventoryTests
{
    /// <summary>Missing files remain required instead of disappearing from qualification.</summary>
    /// <param name="configuration">The build configuration.</param>
    [TestMethod]
    [DataRow("Debug")]
    [DataRow("Release")]
    public void MissingInstallStillRequiresNativeEditorToolsAndSchemas(string configuration)
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenInventory", Guid.NewGuid().ToString("N"));
        var inventory = EditorArtifactInventory.Create(new(Path.Combine(root, "Editor"), Path.Combine(root, "Engine"), configuration));
        var ids = inventory.Select(static item => item.Id);
        _ = ids.Should().Contain(EditorArtifactInventory.InteropId);
        _ = ids.Should().Contain(EditorArtifactInventory.RuntimeId(configuration));
        _ = ids.Should().Contain(EditorArtifactInventory.ImportToolId);
        _ = ids.Should().Contain(EditorArtifactInventory.RenderSceneId);
        _ = ids.Should().Contain("editor/Schemas/oxygen.scene-descriptor.editor.schema.json");
        _ = ids.Should().Contain("engine/schemas/oxygen.scene-descriptor.schema.json");
    }

    /// <summary>New dependencies are detected independently of the accepted manifest.</summary>
    [TestMethod]
    public void InventoryIncludesAddedDependenciesAndSchemaIdentifiers()
    {
        var root = Directory.CreateTempSubdirectory("OxygenInventory-");
        try
        {
            var editor = Directory.CreateDirectory(Path.Combine(root.FullName, "Editor"));
            var engine = Directory.CreateDirectory(Path.Combine(root.FullName, "Engine"));
            Directory.CreateDirectory(Path.Combine(engine.FullName, "bin"));
            Directory.CreateDirectory(Path.Combine(editor.FullName, "Schemas"));
            File.WriteAllText(Path.Combine(engine.FullName, "bin", "NewDependency.dll"), "binary");
            File.WriteAllText(Path.Combine(editor.FullName, "Schemas", "Extra.schema.json"), """{"$id":"https://oxygen.editor/schemas/extra"}""");
            Directory.CreateDirectory(Path.Combine(editor.FullName, "qualification"));
            File.WriteAllText(Path.Combine(editor.FullName, "qualification", "Debug.json"), "must not hash itself");

            var inventory = EditorArtifactInventory.Create(new(editor.FullName, engine.FullName, "Debug"));

            _ = inventory.Should().ContainSingle(item => item.Id == "engine/bin/NewDependency.dll");
            _ = inventory.Should().ContainSingle(item => item.Id == "editor/Schemas/Extra.schema.json" && item.SchemaId == "https://oxygen.editor/schemas/extra");
            _ = inventory.Should().NotContain(item => item.Id.Contains("qualification/", StringComparison.Ordinal));
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    /// <summary>The SDK comes from the explicit package layout or the checkout's installed configuration.</summary>
    [TestMethod]
    public void DiscoveryPrefersBundledEngineAndOtherwiseUsesInstalledConfiguration()
    {
        var root = Directory.CreateTempSubdirectory("OxygenInstallation-");
        try
        {
            var editor = Directory.CreateDirectory(Path.Combine(root.FullName, "artifacts", "Editor"));
            Directory.CreateDirectory(Path.Combine(root.FullName, "projects", "Oxygen.Engine"));
            var checkout = EditorArtifactInstallation.Discover(editor.FullName, "Release");
            _ = checkout.EngineRoot.Should().Be(Path.Combine(root.FullName, "projects", "Oxygen.Engine", "out", "install", "Release"));
            Directory.CreateDirectory(Path.Combine(editor.FullName, "Engine"));
            var packaged = EditorArtifactInstallation.Discover(editor.FullName, "Release");
            _ = packaged.EngineRoot.Should().Be(Path.Combine(editor.FullName, "Engine"));
            _ = packaged.ManifestPath.Should().Be(Path.Combine(editor.FullName, "qualification", "Release.json"));
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }
}
