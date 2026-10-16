// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Authoring;

/// <summary>Checks the folder, name and file of New Script and New Input File.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetAuthoringTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "OxygenScriptAuthoringTests", Guid.NewGuid().ToString("N"));
    private readonly List<AssetRecord> records = [];

    /// <summary>Initializes a new instance of the <see cref="AssetAuthoringTests"/> class.</summary>
    public AssetAuthoringTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Player"));
        this.Project = new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Script authoring tests",
            Category = Category.Games,
            ProjectRoot = this.root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        };
        var catalog = new Mock<IAssetCatalog>();
        _ = catalog.Setup(value => value.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => [.. this.records]);
        this.Authoring = new(catalog.Object, new NativeAtomicFileStore(new RealFileSystem()));
    }

    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    private ProjectContext Project { get; }

    private AssetAuthoring Authoring { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    /// <summary>A new file goes to the browsed folder, or to the kind's folder when a mount root or nothing is browsed.</summary>
    [TestMethod]
    public void DefaultFolderIsTheBrowsedFolderOrTheKindFolderAtTheMountRoot()
    {
        _ = AssetAuthoring.GetDefaultFolder(this.Project, "/Content/Player", "Scripts").Should().Be("/Content/Player");
        _ = AssetAuthoring.GetDefaultFolder(this.Project, "/Content", "Scripts").Should().Be("/Content/Scripts");
        _ = AssetAuthoring.GetDefaultFolder(this.Project, "/", "Scripts").Should().Be("/Content/Scripts");
        _ = AssetAuthoring.GetDefaultFolder(this.Project, null, "Input").Should().Be("/Content/Input");
    }

    /// <summary>A name that would cook to the same asset as a cataloged file of the kind is rejected.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task NameThatWouldCookToACatalogedScriptIsRejected()
    {
        this.Catalog("/Content/Player/move.luau");
        var token = this.TestContext.CancellationToken;

        _ = (await this.Authoring.ValidateAsync(this.Project, AuthoredAssetKind.Script, "move", "/Content/Player", token).ConfigureAwait(false))
            .Should().Contain("move.luau");
        _ = (await this.Authoring.ValidateAsync(this.Project, AuthoredAssetKind.Script, "jump", "/Content/Player", token).ConfigureAwait(false))
            .Should().BeNull();
        _ = (await this.Authoring.ValidateAsync(this.Project, AuthoredAssetKind.Script, "jump", "/Elsewhere", token).ConfigureAwait(false))
            .Should().NotBeNull();
        _ = (await this.Authoring.ValidateAsync(this.Project, AuthoredAssetKind.InputFile, new string('a', AssetAuthoring.MaxNameLength + 1), "/Content/Player", token).ConfigureAwait(false))
            .Should().NotBeNull();
    }

    /// <summary>The proposed name is the first one the catalog does not hold.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ProposalSkipsCatalogedNames()
    {
        this.Catalog("/Content/Input/NewInput1.input.json");
        this.Catalog("/Content/Input/NewInput2.input-action.json");

        var proposal = await this.Authoring.ProposeAsync(this.Project, AuthoredAssetKind.InputFile, "/Content", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proposal.Should().Be(new AssetProposal("/Content/Input", "NewInput3"));
    }

    /// <summary>An input file is written from its template, with its mapping context named after it, and reported.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task InputFileIsWrittenFromItsTemplate()
    {
        var reports = new List<string>();
        this.Authoring.FilesChanged += (_, args) => reports.AddRange(args.CreatedFiles);

        var path = await this.Authoring.CreateAsync(this.Project, AuthoredAssetKind.InputFile, "Player", "/Content/Input", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = path.Should().Be(Path.Combine(this.root, "Content", "Input", "Player.input.json"));
        _ = reports.Should().Equal(path);
        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(AssetTemplates.InputFile("Player"));
    }

    /// <summary>A new scene goes to the Scenes folder at a mount root, named after its file, with an identity of its own.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SceneIsWrittenEmptyWithItsOwnIdentity()
    {
        var proposal = await this.Authoring.ProposeAsync(this.Project, AuthoredAssetKind.Scene, "/Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = proposal.Should().Be(new AssetProposal("/Content/Scenes", "NewScene1"));
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Scenes"));

        var first = await this.Authoring.CreateAsync(this.Project, AuthoredAssetKind.Scene, "Intro", "/Content/Scenes", this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await this.Authoring.CreateAsync(this.Project, AuthoredAssetKind.Scene, "Outro", "/Content/Scenes", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = first.Should().Be(Path.Combine(this.root, "Content", "Scenes", "Intro.oscene.json"));
        using var intro = JsonDocument.Parse(await File.ReadAllTextAsync(first, this.TestContext.CancellationToken).ConfigureAwait(false));
        using var outro = JsonDocument.Parse(await File.ReadAllTextAsync(second, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = intro.RootElement.GetProperty("Name").GetString().Should().Be("Intro");
        _ = intro.RootElement.GetProperty("RootNodes").GetArrayLength().Should().Be(0);
        _ = intro.RootElement.GetProperty("Id").GetGuid().Should().NotBe(outro.RootElement.GetProperty("Id").GetGuid());
    }

    /// <summary>A file the catalog has not seen yet is never overwritten.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ExistingFileIsNeverOverwritten()
    {
        var existing = Path.Combine(this.root, "Content", "Player", "move.lua");
        await File.WriteAllTextAsync(existing, "-- mine", this.TestContext.CancellationToken).ConfigureAwait(false);

        var create = () => this.Authoring.CreateAsync(this.Project, AuthoredAssetKind.Script, "move", "/Content/Player", this.TestContext.CancellationToken);

        _ = await create.Should().ThrowAsync<AssetCreationException>().WithMessage("*move.lua*already exists*").ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(existing, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("-- mine");
    }

    private void Catalog(string virtualPath) => this.records.Add(new(new Uri("asset://" + virtualPath)));
}
