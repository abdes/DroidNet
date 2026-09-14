// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks that startup and cooking require only their own dependencies.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeArtifactInventoryTests
{
    /// <summary>Startup never depends on UI DLLs, RenderScene, the cooker, or a qualification file.</summary>
    /// <param name="configuration">The build configuration.</param>
    [TestMethod]
    [DataRow("Debug")]
    [DataRow("Release")]
    public void StartupRequiresOnlyInteropAndNativeLibraries(string configuration)
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        var inventory = NativeArtifactInventory.CreateRuntime(new(Path.Combine(root, "Editor"), Path.Combine(root, "Engine"), configuration));
        _ = inventory.Select(static item => item.Id).Should().BeEquivalentTo(
            NativeArtifactInventory.InteropId,
            NativeArtifactInventory.RuntimeId(configuration),
            "engine/bin/Oxygen.Engine.EditorInterface" + (string.Equals(configuration, "Debug", StringComparison.Ordinal) ? "-d" : string.Empty) + ".dll");
    }

    /// <summary>Cooking checks tools, producer code and schemas without requiring Interop.</summary>
    [TestMethod]
    public void CookingInventoryIsIndependentOfTheRuntimeBridge()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        var inventory = NativeArtifactInventory.CreateCooking(new(Path.Combine(root, "Editor"), Path.Combine(root, "Engine"), "Debug"));
        _ = inventory.Should().Contain(item => item.Id == NativeArtifactInventory.ImportToolId);
        _ = inventory.Should().Contain(item => item.Id == "editor/Schemas/oxygen.scene-descriptor.schema.json");
        _ = inventory.Should().Contain(item => item.Id == "editor/Schemas/oxygen.import-manifest.schema.json");
        _ = inventory.Should().Contain(item => item.Id == "editor/Schemas/oxygen.scene-source-inspection.schema.json");
        _ = inventory.Should().Contain(item => item.Id == "engine/schemas/oxygen.scene-source-inspection.schema.json");
        _ = inventory.Should().NotContain(item => item.Id == NativeArtifactInventory.InteropId || item.Id.EndsWith("RenderScene.exe", StringComparison.Ordinal));
    }

    /// <summary>The SDK comes from the package or the checkout's installed configuration.</summary>
    [TestMethod]
    public void DiscoveryPrefersBundledEngineAndOtherwiseUsesInstalledConfiguration()
    {
        var root = Directory.CreateTempSubdirectory("OxygenInstallation-");
        try
        {
            var editor = Directory.CreateDirectory(Path.Combine(root.FullName, "artifacts", "Editor"));
            Directory.CreateDirectory(Path.Combine(root.FullName, "projects", "Oxygen.Engine"));
            var checkout = EditorNativeInstallation.Discover(editor.FullName, "Release");
            _ = checkout.EngineRoot.Should().Be(Path.Combine(root.FullName, "projects", "Oxygen.Engine", "out", "install", "Release"));
            Directory.CreateDirectory(Path.Combine(editor.FullName, "Engine"));
            var packaged = EditorNativeInstallation.Discover(editor.FullName, "Release");
            _ = packaged.EngineRoot.Should().Be(Path.Combine(editor.FullName, "Engine"));
            _ = packaged.InteropPath.Should().Be(Path.Combine(editor.FullName, "DroidNet.Oxygen.Editor.Interop.dll"));
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }
}
