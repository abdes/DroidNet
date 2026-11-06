// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// A material's texture bindings are checked against the mounts they name, and a problem states what the material writes,
/// what that resolves to and what was looked up.
/// </summary>
[TestClass]
internal sealed class MaterialTextureCheckTests
{
    private const string Root = @"C:\Games\Demo";
    private const string Material = "/Content/Materials/Red.omat";
    private const string Texture = "/Content/Textures/Red.otex";
    private const string Code = "material.texture_reference_invalid";
    private static readonly string Descriptor = Path.Combine(Root, "Content", "Textures", "Red.otex.json");
    private static readonly string Image = Path.Combine(Root, "Content", "Textures", "Red.png");

    private readonly ProjectContext project = CreateProject();

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task AnAuthoredTextureWithItsImagePasses()
    {
        this.WriteTexture();
        this.project.FileSystem.File.WriteAllText(Image, "image");

        _ = (await this.CheckAsync(Texture).ConfigureAwait(false)).Should().BeEmpty();
    }

    [TestMethod]
    public async Task AMissingDescriptorSaysWhatWasWrittenResolvedAndLookedUp()
    {
        var problem = (await this.CheckAsync(Texture).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(Code);
        _ = problem.AffectedVirtualPath.Should().Be(Material);
        _ = problem.AffectedPath.Should().Be(Descriptor);
        _ = problem.Message.Should().Contain("channel 'base_color'")
            .And.Contain("writes '../Textures/Red.otex'")
            .And.Contain($"which is '{Texture}'")
            .And.Contain("texture descriptor is missing")
            .And.Contain($"looked up '{Descriptor}'");
    }

    [TestMethod]
    public async Task AMissingImageNamesTheImage()
    {
        this.WriteTexture();

        var problem = (await this.CheckAsync(Texture).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(Code);
        _ = problem.AffectedPath.Should().Be(Image);
        _ = problem.Message.Should().Contain("source image is missing");
    }

    [TestMethod]
    public async Task AnInvalidDescriptorKeepsTheReaderExplanation()
    {
        this.project.FileSystem.File.WriteAllText(Descriptor, """{ "unknown": 1 }""");

        var problem = (await this.CheckAsync(Texture).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(Code);
        _ = problem.Message.Should().Contain("texture descriptor is invalid");
        _ = problem.TechnicalMessage.Should().NotBeNullOrEmpty();
    }

    [TestMethod]
    public async Task ALibraryThatHoldsTheTexturePasses()
        => _ = (await this.CheckAsync("/Vendor/Wood.otex").ConfigureAwait(false)).Should().BeEmpty();

    [TestMethod]
    [DataRow("/Vendor/Removed.otex", "Vendor")]
    [DataRow("/Unbound/Wood.otex", "Unbound")]
    public async Task ALibraryThatDoesNotHoldTheTextureSaysSo(string texture, string library)
    {
        var problem = (await this.CheckAsync(texture).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(Code);
        _ = problem.Message.Should().Contain($"writes '{texture}'").And.Contain($"the library '{library}' does not hold");
    }

    [TestMethod]
    public async Task AReferenceTheGrammarRefusesIsTheMaterialsOwnProblem()
    {
        var document = AuthoredFiles.Material(Material).Set("/textures/base_color/virtual_path", Texture);

        var problem = (await this.CheckAsync(document).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be("vpath.reference_self_absolute");
        _ = problem.AffectedVirtualPath.Should().Be(Material);
    }

    private static ProjectContext CreateProject()
    {
        var fileSystem = new MockFileSystem();
        foreach (var folder in new[] { Path.Combine(Root, "Content", "Textures"), @"E:\Vendor" })
        {
            _ = fileSystem.Directory.CreateDirectory(folder);
        }

        fileSystem.File.WriteAllBytes(@"E:\Vendor\" + LooseCookedIndex.FileName, CookedLibraryFixture.LooseIndex("/Vendor/Wood.otex"));
        _ = TestProject.BindMounts(fileSystem, FileSystemPath.From(Root), ("Vendor", @"E:\Vendor"));
        var info = new ProjectInfo("Demo", Category.Games, Root)
        {
            Mounts =
            [
                new ProjectMount("Content", MountKind.Source, "Content"),
                new ProjectMount("Vendor", MountKind.Cooked),
                new ProjectMount("Unbound", MountKind.Cooked),
            ],
        };
        return ProjectContext.FromProjectInfo(info, fileSystem);
    }

    private void WriteTexture()
        => this.project.FileSystem.File.WriteAllBytes(Descriptor, AuthoredFiles.Texture(Texture, "Red.png").Bytes);

    private Task<System.Collections.Immutable.ImmutableArray<DiagnosticRecord>> CheckAsync(string texture)
        => this.CheckAsync(AuthoredFiles.Material(Material).Texture("base_color", texture));

    private Task<System.Collections.Immutable.ImmutableArray<DiagnosticRecord>> CheckAsync(MaterialDocument document)
    {
        var input = new ContentCookInput(
            document.Identity,
            ContentCookAssetKind.Material,
            "Content",
            "/Content/Materials/Red.omat.json",
            FileSystemPath.From(Path.Combine(Root, "Content", "Materials", "Red.omat.json")),
            ContentCookInputRole.Primary);
        return new MaterialTextureCheck(Guid.NewGuid(), this.project).RunAsync(input, document.Bytes, this.TestContext.CancellationToken);
    }
}
