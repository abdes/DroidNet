// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks result order, summaries, details facts and command text without a running browser.</summary>
[TestClass]
public sealed class ContentExplorerPresentationTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Each field orders the results; a header click on the current field reverses it.</summary>
    [TestMethod]
    public void SortOrdersByFieldAndHeaderClicksReverse()
    {
        var project = TestProject.Create(new MockFileSystem());
        var presentation = new AssetBrowserPresentation();
        var bolt = CreateAsset("/Content/Geometry/Bolt.ogeo", AssetKind.Geometry, AssetCookFreshness.Current);
        var clay = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, AssetCookFreshness.OutOfDate);
        var arch = CreateAsset("/Content/Scenes/Arch.oscene", AssetKind.Scene, AssetCookFreshness.NeedsCooking);
        AssetEntry[] items = [bolt, clay, arch];

        _ = presentation.Sort(items, project.Context).Should().Equal(arch, bolt, clay);
        presentation.SortByCommand.Execute(AssetSortField.Name);
        _ = presentation.SortDescending.Should().BeTrue();
        _ = presentation.Sort(items, project.Context).Should().Equal(clay, bolt, arch);

        presentation.SortByCommand.Execute(AssetSortField.Type);
        _ = presentation.SortDescending.Should().BeFalse();
        _ = presentation.Sort(items, project.Context).Should().Equal(bolt, clay, arch);

        // Cooked, Needs cooking and Out of date sort as displayed text.
        presentation.OrderByCommand.Execute(AssetSortField.Status);
        _ = presentation.Sort(items, project.Context).Should().Equal(bolt, arch, clay);

        presentation.OrderByCommand.Execute(AssetSortField.Location);
        presentation.OrderDescendingCommand.Execute(parameter: null);
        _ = presentation.Sort(items, project.Context).Should().Equal(arch, clay, bolt);
    }

    /// <summary>Size and time order the project's files; assets without a file stay last in both directions.</summary>
    [TestMethod]
    public void FileFactsSortWithMissingFilesLast()
    {
        var project = TestProject.Create(new MockFileSystem());
        var files = project.Context.FileSystem;
        var small = project.WriteFile("/Content/Materials/Small.omat.json", "{}");
        var large = project.WriteFile("/Content/Materials/Large.omat.json", new string('x', 4096));
        files.File.SetLastWriteTimeUtc(small.FullPath, new DateTime(2026, 1, 2, 0, 0, 0, DateTimeKind.Utc));
        files.File.SetLastWriteTimeUtc(large.FullPath, new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc));
        var smallAsset = CreateAsset("/Content/Materials/Small.omat", AssetKind.Material, AssetCookFreshness.Current) with { DescriptorPath = small };
        var largeAsset = CreateAsset("/Content/Materials/Large.omat", AssetKind.Material, AssetCookFreshness.Current) with { DescriptorPath = large };
        var missing = CreateAsset("/Content/Materials/Missing.omat", AssetKind.Material, AssetCookFreshness.Current);
        var presentation = new AssetBrowserPresentation { SortField = AssetSortField.Size };

        _ = presentation.Sort([missing, largeAsset, smallAsset], project.Context).Should().Equal(smallAsset, largeAsset, missing);
        presentation.SortDescending = true;
        _ = presentation.Sort([missing, smallAsset, largeAsset], project.Context).Should().Equal(largeAsset, smallAsset, missing);
        presentation.SortField = AssetSortField.Modified;
        _ = presentation.Sort([missing, largeAsset, smallAsset], project.Context).Should().Equal(smallAsset, largeAsset, missing);
        _ = AssetFileFacts.Read(project.Context, largeAsset).SizeText.Should().Be(AssetFileFacts.FormatSize(4096));
    }

    /// <summary>Sort changes are announced once; tile sizes stay within the slider range.</summary>
    [TestMethod]
    public void PresentationAnnouncesSortAndClampsTileSize()
    {
        var presentation = new AssetBrowserPresentation();
        var changes = 0;
        presentation.SortChanged += (_, _) => changes++;
        presentation.OrderByCommand.Execute(AssetSortField.Modified);
        presentation.OrderDescendingCommand.Execute(parameter: null);
        _ = changes.Should().Be(2);
        _ = presentation.SortLabel.Should().Be("Modified");

        presentation.TileSize = 1000;
        _ = presentation.TileSize.Should().Be(AssetBrowserPresentation.MaxTileSize);
        presentation.TileSize = double.NaN;
        _ = presentation.TileSize.Should().Be(AssetBrowserPresentation.MinTileSize);
    }

    /// <summary>The footer counts results against the folder and adds the selection.</summary>
    [TestMethod]
    public void FooterSummaryCountsResultsAndSelection()
    {
        _ = ContentExplorerViewModel.BuildResultSummary(6, 6, 0).Should().Be("6 assets");
        _ = ContentExplorerViewModel.BuildResultSummary(1, 1, 1).Should().Be("1 asset · 1 selected");
        _ = ContentExplorerViewModel.BuildResultSummary(2, 6, 1).Should().Be("2 of 6 assets · 1 selected");
    }

    /// <summary>The footer tells authored locations from read-only mounted and derived ones.</summary>
    [TestMethod]
    public void FooterDescribesAuthoredAndReadOnlySources()
    {
        var project = TestProject.Create(new MockFileSystem(), "C:/Sources").Context with
        {
            Name = "Sources",
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
        };
        _ = ContentExplorerViewModel.DescribeSource(project, AssetPath.Parse("/Content/Materials")).Should().Be("Authoring source");
        _ = ContentExplorerViewModel.DescribeSource(project, AssetPath.Parse("/Content/Scenes")).Should().Be("Authoring source");
        _ = ContentExplorerViewModel.DescribeSource(project, AssetPath.Parse("/Cooked/Content")).Should().Be("Read-only source");
        _ = ContentExplorerViewModel.DescribeSource(project, AssetPath.Parse("/Studio")).Should().Be("Read-only source");
        _ = ContentExplorerViewModel.DescribeSource(project, folder: null).Should().Be("All project sources");
    }

    /// <summary>Copy path places one logical path per selected asset, unescaped.</summary>
    [TestMethod]
    public void CopyPathTextListsLogicalPaths()
    {
        var first = CreateAsset("/Content/Scenes/Evening study.oscene", AssetKind.Scene, AssetCookFreshness.Current);
        var second = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, AssetCookFreshness.Current);
        _ = AssetsViewModel.GetCopyPathText([first, second]).Should().Be(
            "/Content/Scenes/Evening study.oscene" + Environment.NewLine + "/Content/Materials/Clay.omat");
        _ = AssetsViewModel.GetCopyPathText([]).Should().BeEmpty();
    }

    /// <summary>Details list identity facts with wrap points in paths, and summarize a multi-selection.</summary>
    [TestMethod]
    public void DetailsFactsDescribeOneAssetAndSummarizeMany()
    {
        var clay = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, AssetCookFreshness.Current) with { AssetGuid = "1234" };
        var facts = AssetDetailsViewModel.BuildFacts(clay, default);
        _ = facts.Select(static fact => fact.Label).Should().StartWith(["Type", "Logical path", "Identity"]);
        _ = facts[1].Value.Replace("\u200B", string.Empty, StringComparison.Ordinal).Should().Be("/Content/Materials/Clay.omat");
        _ = facts[1].Value.Should().Contain("/\u200B");

        var bolt = CreateAsset("/Content/Geometry/Bolt.ogeo", AssetKind.Geometry, AssetCookFreshness.OutOfDate);
        var stone = CreateAsset("/Content/Materials/Stone.omat", AssetKind.Material, AssetCookFreshness.Current);
        var summary = AssetDetailsViewModel.BuildSummaryFacts([clay, bolt, stone]);
        _ = summary.Single(static fact => string.Equals(fact.Label, "Types", StringComparison.Ordinal)).Value.Should().Be("Material: 2" + Environment.NewLine + "Geometry: 1");
        _ = summary.Single(static fact => string.Equals(fact.Label, "Statuses", StringComparison.Ordinal)).Value.Should().Contain("Out of date: 1");
    }

    /// <summary>The details pane follows the published selection and copies its paths.</summary>
    [TestMethod]
    public void DetailsPaneFollowsSelectionAndCopiesPaths()
    {
        var state = new ContentExplorerState();
        var shell = new RecordingShell();
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem()).Context);
        using var details = new AssetDetailsViewModel(state, projects, shell);
        _ = details.IsEmpty.Should().BeTrue();
        _ = details.CopyPathCommand.CanExecute(parameter: null).Should().BeFalse();

        var clay = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, AssetCookFreshness.Current);
        state.PublishSelection([clay]);
        _ = details.IsSingle.Should().BeTrue();
        _ = details.Title.Should().Be("Clay.omat");
        _ = details.LocateCommand.CanExecute(parameter: null).Should().BeFalse("the test asset has no file on disk");

        var bolt = CreateAsset("/Content/Geometry/Bolt.ogeo", AssetKind.Geometry, AssetCookFreshness.Current);
        state.PublishSelection([clay, bolt]);
        _ = details.IsMultiple.Should().BeTrue();
        _ = details.Title.Should().Be("2 assets selected");
        _ = details.SelectedItems.Select(static item => item.Label).Should().Equal("Clay.omat", "Bolt.ogeo");
        details.CopyPathCommand.Execute(parameter: null);
        _ = shell.Copied.Should().Be("/Content/Materials/Clay.omat" + Environment.NewLine + "/Content/Geometry/Bolt.ogeo");
    }

    /// <summary>New folders take the first free name, like File Explorer.</summary>
    [TestMethod]
    public void NewFolderNamesAreUnique()
    {
        _ = ContentFoldersViewModel.GetUniqueFolderName(static _ => false).Should().Be("New folder");
        var taken = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "New folder", "New folder (2)" };
        _ = ContentFoldersViewModel.GetUniqueFolderName(taken.Contains).Should().Be("New folder (3)");
    }

    /// <summary>Selected filters become chips; removing a chip clears only that filter.</summary>
    [TestMethod]
    public void FilterChipsListAndRemoveSelectedFilters()
    {
        var query = new AssetBrowserQuery { SearchText = "clay" };
        query.TypeOptions.Single(static option => string.Equals(option.Label, "Materials", StringComparison.Ordinal)).IsSelected = true;
        query.StatusOptions.Single(static option => string.Equals(option.Label, "Out of date", StringComparison.Ordinal)).IsSelected = true;
        _ = query.HasActiveFilters.Should().BeTrue();
        _ = query.ActiveFilters.Select(static option => option.Label).Should().Equal("Materials", "Out of date");

        query.ActiveFilters[0].RemoveCommand.Execute(parameter: null);
        _ = query.ActiveFilters.Select(static option => option.Label).Should().Equal("Out of date");
        _ = query.SearchText.Should().Be("clay");
    }

    /// <summary>Every displayed status maps to a dot tone, including the browser's own badges.</summary>
    [TestMethod]
    public void StatusTonesCoverExplorerBadges()
    {
        _ = AssetStatusText.GetToneForText("Ready").Should().Be("Success");
        _ = AssetStatusText.GetToneForText("Unsaved changes").Should().Be("Caution");
        _ = AssetStatusText.GetToneForText("Overridden").Should().Be("Caution");
        _ = AssetStatusText.GetToneForText("ERR").Should().Be("Critical");
        _ = AssetStatusText.GetToneForText("Engine").Should().Be("Neutral");
    }

    /// <summary>Sizes read like File Explorer's.</summary>
    [TestMethod]
    public void SizesUseBinaryUnits()
    {
        _ = AssetFileFacts.FormatSize(512).Should().Be(string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{512:N0} bytes"));
        _ = AssetFileFacts.FormatSize(31 * 1024).Should().Be("31 KB");
        _ = AssetFileFacts.FormatSize((long)(1.5 * 1024 * 1024)).Should().Be(string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{1.5:0.#} MB"));
    }

    private static AssetEntry CreateAsset(string path, AssetKind kind, AssetCookFreshness freshness)
    {
        var identity = AssetPath.Parse(path);
        var current = freshness == AssetCookFreshness.Current;
        return new(
            identity,
            Path.GetFileName(path),
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            path,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            CookStatus = new(identity, freshness, HasPublishedOutput: current, OutputAvailability: current ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [], []),
        };
    }

    private sealed class RecordingShell : IDesktopIntegration
    {
        public string? Copied { get; private set; }

        public void CopyText(string text) => this.Copied = text;

        public bool ShowInFileExplorer(string path) => true;

        public void MoveToRecycleBin(IReadOnlyList<string> paths)
        {
        }
    }
}
