// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Input;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Authoring;

/// <summary>Checks the folder, name and file of a new file created from its family's template.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetAuthoringTests
{
    private readonly List<AssetRecord> records = [];

    /// <summary>Initializes a new instance of the <see cref="AssetAuthoringTests"/> class.</summary>
    public AssetAuthoringTests()
    {
        var fileSystem = new MockFileSystem();
        this.Project = TestProject.Create(fileSystem);
        _ = fileSystem.Directory.CreateDirectory(this.Project.PathOf("/Content/Player").FullPath);
        var catalog = new Mock<IAssetCatalog>();
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => [.. this.records]);
        this.Authoring = new(catalog.Object, new NativeAtomicFileStore(fileSystem));
    }

    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    private TestProject Project { get; }

    private ProjectContext Context => this.Project.Context;

    private AssetAuthoring Authoring { get; }

    /// <summary>A new file goes to the browsed folder, or to the template's folder when a mount root or nothing is browsed.</summary>
    [TestMethod]
    public void DefaultFolderIsTheBrowsedFolderOrTheTemplateFolderAtTheMountRoot()
    {
        _ = AssetAuthoring.GetDefaultFolder(this.Context, AssetPath.Parse("/Content/Player"), "Scripts").Should().Be(AssetPath.Parse("/Content/Player"));
        _ = AssetAuthoring.GetDefaultFolder(this.Context, AssetPath.Parse("/Content"), "Scripts").Should().Be(AssetPath.Parse("/Content/Scripts"));
        _ = AssetAuthoring.GetDefaultFolder(this.Context, selectedFolder: null, "Scripts").Should().Be(AssetPath.Parse("/Content/Scripts"));
        _ = AssetAuthoring.GetDefaultFolder(this.Context, selectedFolder: null, "Input").Should().Be(AssetPath.Parse("/Content/Input"));
    }

    /// <summary>A name that would cook to the same asset as a cataloged file with any of the template's suffixes is rejected.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task NameThatWouldCookToACatalogedFileIsRejected()
    {
        this.Catalog("/Content/Player/move.luau");
        var token = this.TestContext.CancellationToken;

        _ = (await this.Authoring.ValidateAsync(this.Context, ScriptTemplates.Asset, "move", AssetPath.Parse("/Content/Player"), token).ConfigureAwait(false))
            .Should().Contain("move.luau");
        _ = (await this.Authoring.ValidateAsync(this.Context, ScriptTemplates.Asset, "jump", AssetPath.Parse("/Content/Player"), token).ConfigureAwait(false))
            .Should().BeNull();
        _ = (await this.Authoring.ValidateAsync(this.Context, ScriptTemplates.Asset, "jump", AssetPath.Parse("/Elsewhere"), token).ConfigureAwait(false))
            .Should().NotBeNull();
        _ = (await this.Authoring.ValidateAsync(this.Context, InputTemplates.Asset, new string('a', 64), AssetPath.Parse("/Content/Player"), token).ConfigureAwait(false))
            .Should().BeNull();
        _ = (await this.Authoring.ValidateAsync(this.Context, InputTemplates.Asset, new string('a', 256), AssetPath.Parse("/Content/Player"), token).ConfigureAwait(false))
            .Should().NotBeNull();
    }

    /// <summary>The proposed name is the first one the catalog does not hold.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ProposalSkipsCatalogedNames()
    {
        this.Catalog("/Content/Input/NewInput1.input.json");
        this.Catalog("/Content/Input/NewInput2.input.json");

        var proposal = await this.Authoring.ProposeAsync(this.Context, InputTemplates.Asset, AssetPath.Parse("/Content"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proposal.Should().Be(new AssetProposal(AssetPath.Parse("/Content/Input"), "NewInput3"));
    }

    /// <summary>A file is written with the template's first suffix and the content it makes from the name, and reported.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FileIsWrittenFromItsTemplateAndReported()
    {
        var reports = new List<FileSystemPath>();
        this.Authoring.FilesChanged += (_, args) => reports.AddRange(args.CreatedFiles);

        var path = await this.Authoring.CreateAsync(this.Context, InputTemplates.Asset, "Player", AssetPath.Parse("/Content/Input"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = path.Should().Be(this.Project.PathOf("/Content/Input/Player.input.json"));
        _ = reports.Should().Equal(path);
        _ = (await this.Project.Context.FileSystem.File.ReadAllTextAsync(path.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(InputTemplates.New("Player"));
    }

    /// <summary>A file the catalog has not seen yet is never overwritten.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ExistingFileIsNeverOverwritten()
    {
        var existing = this.Project.WriteFile("/Content/Player/move.lua", "-- mine");

        var create = () => this.Authoring.CreateAsync(this.Context, ScriptTemplates.Asset, "move", AssetPath.Parse("/Content/Player"), this.TestContext.CancellationToken);

        _ = await create.Should().ThrowAsync<AssetCreationException>().ConfigureAwait(false);
        _ = (await this.Project.Context.FileSystem.File.ReadAllTextAsync(existing.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("-- mine");
    }

    private void Catalog(string virtualPath) => this.records.Add(new(AssetPath.Parse(virtualPath)));
}
