// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class NativeCompatibilityTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>The deployed editor schema names and installed cooker pass the real preflight.</summary>
    /// <returns>The asynchronous installed-input check.</returns>
    [TestMethod]
    public async Task InstalledCookerInputsMatchEditorSchemas()
    {
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var result = await compatibility.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue(string.Join("; ", result.Diagnostics.Select(static value => value.TechnicalMessage ?? value.Message)));
        await result.Artifacts!.DisposeAsync().ConfigureAwait(false);
    }

    /// <summary>The installed SDK supplies the real catalog through the ordinary compatibility and worker path.</summary>
    /// <returns>The asynchronous native catalog check.</returns>
    [TestMethod]
    public async Task InstalledSdkProvidesBuiltinCatalog()
    {
        var root = Directory.CreateTempSubdirectory("OxygenInstalledCatalog-");
        try
        {
            using var compatibility = EditorNativeCompatibilityService.ForCooking();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var catalog = await api.GetBuiltinGeometryCatalogAsync(root.FullName, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = catalog.AuthoringGeometries.Should().Contain(value => string.Equals(value.CanonicalName, "Cube", StringComparison.Ordinal));
            _ = catalog.MountName.Should().Be("Content");
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }
}
