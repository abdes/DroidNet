// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks editor shader selection against the installed SDK layout.</summary>
[TestClass]
public sealed class NativeShaderConfigurationTests
{
    /// <summary>Default startup uses the selected SDK regardless of the application's working directory.</summary>
    [TestMethod]
    public void DefaultsUseInstalledSdkAndDoNotPersistMachinePaths()
    {
        using var fixture = new Fixture();
        var settings = new EngineSettings();
        settings.Renderer.PathFinder.WorkspaceRootPath = fixture.Root;
        var config = NativeEngineSession.CreateConfig(settings, null, fixture.RuntimeLibrary);
        _ = config.Engine.PathFinder.ShaderLibraryPath.Should().Be(fixture.Archive);
        _ = config.Renderer.PathFinder.ShaderLibraryPath.Should().Be(fixture.Archive);
        _ = settings.Engine.PathFinder.ShaderLibraryPath.Should().BeNull();
        _ = settings.Renderer.PathFinder.ShaderLibraryPath.Should().BeNull();
    }

    /// <summary>Explicit development archives keep their native path-resolution semantics.</summary>
    [TestMethod]
    public void ExplicitArchivesRemainIndependent()
    {
        using var fixture = new Fixture();
        var settings = new EngineSettings();
        settings.Engine.PathFinder.ShaderLibraryPath = "custom/engine.bin";
        settings.Renderer.PathFinder.ShaderLibraryPath = "custom/renderer.bin";
        var config = NativeEngineSession.CreateConfig(settings, null, fixture.RuntimeLibrary);
        _ = config.Engine.PathFinder.ShaderLibraryPath.Should().Be("custom/engine.bin");
        _ = config.Renderer.PathFinder.ShaderLibraryPath.Should().Be("custom/renderer.bin");
    }

    /// <summary>An incomplete SDK fails startup instead of silently running an empty shader cache.</summary>
    [TestMethod]
    public void MissingInstalledArchiveReportsItsExactPath()
    {
        using var fixture = new Fixture();
        File.Delete(fixture.Archive);
        Action configure = () => _ = NativeEngineSession.CreateConfig(new EngineSettings(), null, fixture.RuntimeLibrary);
        _ = configure.Should().Throw<FileNotFoundException>().Which.FileName.Should().Be(fixture.Archive);
    }

    private sealed class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenShaderConfiguration-");

        public Fixture()
        {
            this.Installation = new(Path.Combine(this.Root, "Editor"), Path.Combine(this.Root, "Engine"), EditorNativeCompatibilityService.CurrentConfiguration);
            Directory.CreateDirectory(Path.GetDirectoryName(this.Archive)!);
            File.WriteAllBytes(this.Archive, []);
        }

        public string Root => this.directory.FullName;

        public EditorNativeInstallation Installation { get; }

        public string Archive => this.Installation.ShaderLibraryPath;

        public string RuntimeLibrary => Path.Combine(this.Installation.EngineRoot, "bin", Path.GetFileName(NativeArtifactInventory.RuntimeId(EditorNativeCompatibilityService.CurrentConfiguration)));

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
