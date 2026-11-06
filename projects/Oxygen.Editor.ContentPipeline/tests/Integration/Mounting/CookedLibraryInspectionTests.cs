// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Mounting;

/// <summary>
/// A library that stops being readable after the mount table accepted it never joins a cook unnamed: one gone or corrupt is
/// left out and named, and one the Inspector cannot read or finds damaged stops the cook with a located diagnostic.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookedLibraryInspectionTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A library folder deleted after the table resolved it is left out and named.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AMissingLibraryIsLeftOutAndNamed()
    {
        using var workspace = new CookWorkspace();
        var (project, library) = BindLibrary(workspace);
        Directory.Delete(library, recursive: true);

        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, new EngineContentPakFixture(), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = libraries.Unreadable.Should().ContainSingle().Which.Name.Should().Be("Vendor");
    }

    /// <summary>An index corrupted after the table resolved it is left out and named.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACorruptLibraryIsLeftOutAndNamed()
    {
        using var workspace = new CookWorkspace();
        var (project, library) = BindLibrary(workspace);
        await File.WriteAllBytesAsync(Path.Combine(library, LooseCookedIndex.FileName), [1, 2, 3], this.TestContext.CancellationToken).ConfigureAwait(false);

        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, new EngineContentPakFixture(), this.TestContext.CancellationToken).ConfigureAwait(false);

        var unreadable = libraries.Unreadable.Should().ContainSingle().Subject;
        _ = unreadable.Name.Should().Be("Vendor");
        _ = unreadable.Path.Should().Be(library);
    }

    /// <summary>The Inspector failing on a library stops the cook, naming the library and its location.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnInspectorFailureNamesTheLibrary()
    {
        using var workspace = new CookWorkspace();
        var (project, library) = BindLibrary(workspace);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, new EngineContentPakFixture(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var native = new Mock<IEngineContentPipelineApi>();
        _ = native.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new InvalidDataException("The report is corrupt."));

        var validate = () => libraries.ValidateNativeAsync(native.Object, this.TestContext.CancellationToken);

        var failure = (await validate.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false)).Which;
        var diagnostic = failure.Diagnostics.Should().ContainSingle().Subject;
        _ = diagnostic.Code.Should().Be(CookedLibraryReadSet.LibraryInspectionFailed);
        _ = diagnostic.AffectedPath.Should().Be(library);
        _ = diagnostic.Message.Should().Contain("'Vendor'").And.Contain("The report is corrupt.");
    }

    /// <summary>A writer's leftovers do not stop a library being read; damage does.</summary>
    /// <param name="reason">The reason the Inspector reports.</param>
    /// <param name="accepted">Whether the library is accepted.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("uncommitted", true)]
    [DataRow("uncommitted_tail", true)]
    [DataRow("digest_mismatch", false)]
    public async Task OnlyDamageStopsALibraryBeingRead(string reason, bool accepted)
    {
        using var workspace = new CookWorkspace();
        var (project, library) = BindLibrary(workspace);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, new EngineContentPakFixture(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var json = Oxygen.Testing.NativeInventoryFixture.ReadJson(library).Replace("\"issues\":[]", $$"""
            "issues":[{"relative_path":"stray.bin","reason":"{{reason}}"}]
            """, StringComparison.Ordinal);
        var native = new Mock<IEngineContentPipelineApi>();
        _ = native.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(CookedInventoryReport.Parse(json));

        var validate = () => libraries.ValidateNativeAsync(native.Object, this.TestContext.CancellationToken);

        if (accepted)
        {
            await validate.Should().NotThrowAsync().ConfigureAwait(false);
        }
        else
        {
            _ = (await validate.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false)).Which.Diagnostics.Should().ContainSingle()
                .Which.Message.Should().Contain(reason);
        }
    }

    private static (ProjectContext Project, string Library) BindLibrary(CookWorkspace workspace)
    {
        var library = Path.Combine(workspace.Root, "External", "Vendor");
        var descriptor = Path.Combine(library, "Materials", "Wood.omat");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(descriptor)!);
        File.WriteAllText(descriptor, "wood");
        Oxygen.Testing.NativeInventoryFixture.WriteIndex(library, [new CookedAssetEntry("/Vendor/Materials/Wood.omat", ContentCookAssetKind.Material) { DescriptorRelativePath = "Materials/Wood.omat" }]);
        return (workspace.ProjectContext.WithCookedLibraries(("Vendor", library)), library);
    }
}
