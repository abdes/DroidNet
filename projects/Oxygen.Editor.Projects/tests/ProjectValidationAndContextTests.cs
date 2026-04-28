// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.TestHelpers;
using Moq;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.World;
using Oxygen.Storage;
using Oxygen.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ProjectValidation")]
public sealed class ProjectValidationAndContextTests : TestSuiteWithAssertions
{
    [TestMethod]
    public async Task ValidateAsync_ShouldReturnValid_WhenManifestAndContentRootsAreValid()
    {
        var (storage, document) = CreateStorageWithProjectFile();
        var service = new ProjectValidationService(storage.Object);
        document.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>()))
            .ReturnsAsync(CreateManifestJson(schemaVersion: 1));

        var result = await service.ValidateAsync("project").ConfigureAwait(false);

        _ = result.State.Should().Be(ProjectValidationState.Valid);
        _ = result.ProjectInfo.Should().NotBeNull();
        _ = result.ProjectInfo!.Location.Should().Be(@"C:\Project");
    }

    [TestMethod]
    public async Task ValidateAsync_ShouldReturnUnsupportedVersion_WhenManifestSchemaIsUnsupported()
    {
        var (storage, document) = CreateStorageWithProjectFile();
        var service = new ProjectValidationService(storage.Object);
        document.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>()))
            .ReturnsAsync(CreateManifestJson(schemaVersion: 2));

        var result = await service.ValidateAsync("project").ConfigureAwait(false);

        _ = result.State.Should().Be(ProjectValidationState.UnsupportedVersion);
    }

    [TestMethod]
    public async Task ValidateAsync_ShouldReturnInvalidContentRoots_WhenAuthoringMountsAreMissing()
    {
        var (storage, document) = CreateStorageWithProjectFile();
        var service = new ProjectValidationService(storage.Object);
        document.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>()))
            .ReturnsAsync(CreateManifestJson(schemaVersion: 1, includeAuthoringMounts: false));

        var result = await service.ValidateAsync("project").ConfigureAwait(false);

        _ = result.State.Should().Be(ProjectValidationState.InvalidContentRoots);
    }

    [TestMethod]
    public void ProjectContextService_ShouldReplayCurrentValueToNewSubscribers()
    {
        var service = new ProjectContextService();
        var context = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Project",
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };

        service.Activate(context);

        var observer = new RecordingObserver<ProjectContext?>();
        using var subscription = service.ProjectChanged.Subscribe(observer);

        _ = observer.Values.Should().ContainSingle().Which.Should().Be(context);
    }

    [TestMethod]
    public void ProjectContextService_ShouldPublishReplacementAndClose()
    {
        var service = new ProjectContextService();
        var first = CreateContext("First");
        var second = CreateContext("Second");
        var observer = new RecordingObserver<ProjectContext?>();
        using var subscription = service.ProjectChanged.Subscribe(observer);

        service.Activate(first);
        service.Activate(second);
        service.Close();

        _ = observer.Values.Should().Equal(null, first, second, null);
        _ = service.ActiveProject.Should().BeNull();
    }

    [TestMethod]
    public void ProjectCookScopeProvider_ShouldUseProjectCookedOutputRoot()
    {
        var storage = new Mock<IStorageProvider>();
        storage.Setup(s => s.NormalizeRelativeTo(@"C:\Project", Constants.CookedOutputFolderName))
            .Returns(@"C:\Project\.cooked");
        var provider = new ProjectCookScopeProvider(storage.Object);
        var context = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Project",
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [],
            LocalFolderMounts = [],
            Scenes = [],
        };

        var scope = provider.CreateScope(context);

        _ = scope.CookedOutputRoot.Should().Be(@"C:\Project\.cooked");
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldDefaultMaterialCreationToContentMaterials()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project");

        var target = resolver.ResolveCreateTarget(context, AuthoringAssetKind.Material, null);

        _ = target.ProjectRelativeFolder.Should().Be("Content/Materials");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///Content/Materials"));
        _ = target.FallbackReason.Should().Be(AuthoringTargetFallbackReason.NoSelection);
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldIgnoreMismatchedSceneFolderForMaterialCreation()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project");

        var target = resolver.ResolveCreateTarget(
            context,
            AuthoringAssetKind.Material,
            new ContentBrowserSelection("Content/Scenes"));

        _ = target.ProjectRelativeFolder.Should().Be("Content/Materials");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///Content/Materials"));
        _ = target.FallbackReason.Should().Be(AuthoringTargetFallbackReason.KindMismatch);
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldHonorMatchingMaterialSubfolder()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project");

        var target = resolver.ResolveCreateTarget(
            context,
            AuthoringAssetKind.Material,
            new ContentBrowserSelection("Content/Materials/Subfolder"));

        _ = target.ProjectRelativeFolder.Should().Be("Content/Materials/Subfolder");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///Content/Materials/Subfolder"));
        _ = target.FallbackReason.Should().BeNull();
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldSupportNonDefaultContentMountPath()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project") with
        {
            AuthoringMounts = [new ProjectMountPoint("Content", "Authoring")],
        };

        var target = resolver.ResolveCreateTarget(
            context,
            AuthoringAssetKind.Material,
            new ContentBrowserSelection("Authoring/Materials/Subfolder"));

        _ = target.ProjectRelativeFolder.Should().Be("Authoring/Materials/Subfolder");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///Content/Materials/Subfolder"));
        _ = target.FallbackReason.Should().BeNull();
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldHonorExplicitLocalMountMaterialSubfolder()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project") with
        {
            LocalFolderMounts = [new LocalFolderMount("StudioLibrary", @"D:\Studio\SharedContent")],
        };

        var target = resolver.ResolveCreateTarget(
            context,
            AuthoringAssetKind.Material,
            new ContentBrowserSelection("StudioLibrary/Materials/Shared", "StudioLibrary"));

        _ = target.IsExplicitLocalMount.Should().BeTrue();
        _ = target.ProjectRelativeFolder.Should().Be("Materials/Shared");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///StudioLibrary/Materials/Shared"));
        _ = target.FallbackReason.Should().BeNull();
    }

    [TestMethod]
    public void AuthoringTargetResolver_ShouldFallbackWhenExplicitLocalMountSelectionIsWrongKind()
    {
        var resolver = new AuthoringTargetResolver();
        var context = CreateContext("Project") with
        {
            LocalFolderMounts = [new LocalFolderMount("StudioLibrary", @"D:\Studio\SharedContent")],
        };

        var target = resolver.ResolveCreateTarget(
            context,
            AuthoringAssetKind.Material,
            new ContentBrowserSelection("StudioLibrary/Scenes", "StudioLibrary"));

        _ = target.IsExplicitLocalMount.Should().BeTrue();
        _ = target.ProjectRelativeFolder.Should().Be("Materials");
        _ = target.FolderAssetUri.Should().Be(new Uri("asset:///StudioLibrary/Materials"));
        _ = target.FallbackReason.Should().Be(AuthoringTargetFallbackReason.KindMismatch);
    }

    [TestMethod]
    public async Task ProjectCreationService_ShouldCreateV01ManifestAndValidateResult()
    {
        var fs = new MockFileSystem();
        CreateTemplateSkeleton(fs, @"C:\Templates\Blank");
        fs.Directory.CreateDirectory(@"C:\Projects");
        await fs.File.WriteAllTextAsync(@"C:\Templates\Blank\Template.json", CreateTemplateDescriptorJson()).ConfigureAwait(false);
        await fs.File.WriteAllTextAsync(@"C:\Templates\Blank\Content\asset.txt", "payload").ConfigureAwait(false);

        var storage = new NativeStorageProvider(fs);
        var service = new ProjectCreationService(storage, new ProjectValidationService(storage));

        var result = await service.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = @"C:\Templates\Blank",
                    ParentLocation = @"C:\Projects",
                    ProjectName = "Created",
                    Category = Category.Games,
                })
            .ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = fs.File.Exists(@"C:\Projects\Created\Project.oxy").Should().BeTrue();
        _ = fs.File.Exists(@"C:\Projects\Created\Template.json").Should().BeFalse();
        _ = fs.File.Exists(@"C:\Projects\Created\Content\asset.txt").Should().BeTrue();

        var manifest = await fs.File.ReadAllTextAsync(@"C:\Projects\Created\Project.oxy").ConfigureAwait(false);
        var info = ProjectInfo.FromJson(manifest);
        _ = info.SchemaVersion.Should().Be(ProjectInfo.CurrentSchemaVersion);
        _ = info.Name.Should().Be("Created");
        _ = info.Id.Should().NotBeEmpty();
        _ = info.AuthoringMounts.Should().ContainSingle();
        _ = manifest.Should().NotContain(nameof(ProjectInfo.Location));
        _ = manifest.Should().NotContain(nameof(ProjectInfo.LastUsedOn));
        _ = fs.Directory.Exists(@"C:\Projects\Created\Content\Scenes").Should().BeTrue();
        _ = fs.Directory.Exists(@"C:\Projects\Created\Content\Materials").Should().BeTrue();
        _ = result.OpenStarterScene.Should().BeTrue();
        _ = result.StarterSceneAssetUri.Should().Be(new Uri("asset:///Content/Scenes/Main.oscene.json"));
    }

    [TestMethod]
    public async Task ProjectCreationService_ShouldCreateUniqueProjectIdsFromDescriptorTemplate()
    {
        var fs = new MockFileSystem();
        CreateTemplateSkeleton(fs, @"C:\Templates\Blank");
        fs.Directory.CreateDirectory(@"C:\Projects");
        await fs.File.WriteAllTextAsync(@"C:\Templates\Blank\Template.json", CreateTemplateDescriptorJson()).ConfigureAwait(false);

        var storage = new NativeStorageProvider(fs);
        var service = new ProjectCreationService(storage, new ProjectValidationService(storage));

        var first = await service.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = @"C:\Templates\Blank",
                    ParentLocation = @"C:\Projects",
                    ProjectName = "First",
                    Category = Category.Games,
                })
            .ConfigureAwait(false);
        var second = await service.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = @"C:\Templates\Blank",
                    ParentLocation = @"C:\Projects",
                    ProjectName = "Second",
                    Category = Category.Games,
                })
            .ConfigureAwait(false);

        _ = first.Succeeded.Should().BeTrue(first.Message);
        _ = second.Succeeded.Should().BeTrue(second.Message);
        _ = first.ProjectInfo!.Id.Should().NotBe(second.ProjectInfo!.Id);
        _ = first.ProjectInfo.Id.Should().NotBeEmpty();
        _ = second.ProjectInfo.Id.Should().NotBeEmpty();
    }

    [TestMethod]
    public async Task ProjectCreationService_ShouldRejectTemplatePayloadProjectManifest()
    {
        var fs = new MockFileSystem();
        CreateTemplateSkeleton(fs, @"C:\Templates\Blank");
        fs.Directory.CreateDirectory(@"C:\Projects");
        await fs.File.WriteAllTextAsync(@"C:\Templates\Blank\Template.json", CreateTemplateDescriptorJson()).ConfigureAwait(false);
        await fs.File.WriteAllTextAsync(@"C:\Templates\Blank\Project.oxy", CreateManifestJson(schemaVersion: 1))
            .ConfigureAwait(false);

        var storage = new NativeStorageProvider(fs);
        var service = new ProjectCreationService(storage, new ProjectValidationService(storage));

        var result = await service.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = @"C:\Templates\Blank",
                    ParentLocation = @"C:\Projects",
                    ProjectName = "Created",
                    Category = Category.Games,
                })
            .ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Message.Should().Contain("Project.oxy");
    }

    [TestMethod]
    public async Task ProjectCreationService_ShouldRejectInvalidTemplateDescriptor()
    {
        var fs = new MockFileSystem();
        CreateTemplateSkeleton(fs, @"C:\Templates\Blank");
        fs.Directory.CreateDirectory(@"C:\Projects");
        await fs.File.WriteAllTextAsync(
                @"C:\Templates\Blank\Template.json",
                CreateTemplateDescriptorJson().Replace("\"SchemaVersion\": 1", "\"SchemaVersion\": 2", StringComparison.Ordinal))
            .ConfigureAwait(false);

        var storage = new NativeStorageProvider(fs);
        var service = new ProjectCreationService(storage, new ProjectValidationService(storage));

        var result = await service.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = @"C:\Templates\Blank",
                    ParentLocation = @"C:\Projects",
                    ProjectName = "Created",
                    Category = Category.Games,
                })
            .ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Message.Should().Contain("SchemaVersion");
    }

    [TestMethod]
    public async Task RecentProjectAdapter_ShouldClassifyStaleProjectsWithoutDeletingUsage()
    {
        var projectUsage = new Mock<IProjectUsageService>();
        var validation = new Mock<IProjectValidationService>();
        projectUsage.Setup(p => p.GetMostRecentlyUsedProjectsAsync(10))
            .ReturnsAsync(
                [
                    new ProjectUsage
                    {
                        Name = "Missing",
                        Location = @"C:\Missing",
                        LastUsedOn = new DateTime(year: 2026, month: 1, day: 2),
                    },
                ]);
        validation.Setup(v => v.ValidateAsync(@"C:\Missing", It.IsAny<CancellationToken>()))
            .ReturnsAsync(
                ProjectValidationResult.Failure(
                    ProjectValidationState.Missing,
                    @"C:\Missing",
                    "Project folder does not exist."));
        var adapter = new RecentProjectAdapter(projectUsage.Object, validation.Object);

        var entries = new List<RecentProjectEntry>();
        await foreach (var entry in adapter.GetRecentProjectsAsync().ConfigureAwait(false))
        {
            entries.Add(entry);
        }

        _ = entries.Should().ContainSingle();
        _ = entries[0].Validation.State.Should().Be(ProjectValidationState.Missing);
        _ = entries[0].IsUsable.Should().BeFalse();
        projectUsage.Verify(p => p.DeleteProjectUsageAsync(It.IsAny<string>(), It.IsAny<string>()), Times.Never);
    }

    [TestMethod]
    public async Task RecentProjectAdapter_ShouldRecordActivatedProjectUsage()
    {
        var projectUsage = new Mock<IProjectUsageService>();
        var validation = new Mock<IProjectValidationService>();
        var adapter = new RecentProjectAdapter(projectUsage.Object, validation.Object);
        var context = CreateContext("Project");

        await adapter.RecordActivatedAsync(context).ConfigureAwait(false);

        projectUsage.Verify(p => p.UpdateProjectUsageAsync("Project", @"C:\Project"), Times.Once);
    }

    private static (Mock<IStorageProvider> storage, Mock<IDocument> document) CreateStorageWithProjectFile()
    {
        var storage = new Mock<IStorageProvider>();
        var document = new Mock<IDocument>();

        storage.Setup(s => s.Normalize("project")).Returns(@"C:\Project");
        storage.Setup(s => s.FolderExistsAsync(@"C:\Project")).ReturnsAsync(true);
        storage.Setup(s => s.NormalizeRelativeTo(@"C:\Project", Constants.ProjectFileName))
            .Returns(@"C:\Project\Project.oxy");
        storage.Setup(s => s.DocumentExistsAsync(@"C:\Project\Project.oxy")).ReturnsAsync(true);
        storage.Setup(s => s.GetDocumentFromPathAsync(@"C:\Project\Project.oxy", It.IsAny<CancellationToken>()))
            .ReturnsAsync(document.Object);
        storage.Setup(s => s.NormalizeRelativeTo(@"C:\Project", "Content"))
            .Returns(@"C:\Project\Content");
        storage.Setup(s => s.FolderExistsAsync(@"C:\Project\Content")).ReturnsAsync(true);

        return (storage, document);
    }

    private static string CreateManifestJson(int schemaVersion, bool includeAuthoringMounts = true)
    {
        var authoringMounts = includeAuthoringMounts
            ? """
              ,
                "AuthoringMounts": [
                  {
                    "Name": "Content",
                    "RelativePath": "Content"
                  }
                ]
              """
            : string.Empty;

        return $$"""
                 {
                   "SchemaVersion": {{schemaVersion}},
                   "Id": "{{Guid.NewGuid()}}",
                   "Name": "Project",
                   "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
                   "Thumbnail": "Media/Preview.png"{{authoringMounts}},
                   "LocalFolderMounts": []
                 }
                 """;
    }

    private static string CreateTemplateDescriptorJson()
        => """
           {
             "SchemaVersion": 1,
             "TemplateId": "Games/Blank",
             "Name": "Blank",
             "DisplayName": "Blank",
             "Description": "Blank template",
             "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
             "Icon": "Media/Icon.png",
             "Preview": "Media/Preview.png",
             "SourceFolder": ".",
             "Thumbnail": "Media/Preview.png",
             "AuthoringMounts": [
               {
                 "Name": "Content",
                 "RelativePath": "Content"
               }
             ],
             "LocalFolderMounts": [],
             "StarterScene": {
               "AssetUri": "asset:///Content/Scenes/Main.oscene.json",
               "RelativePath": "Content/Scenes/Main.oscene.json",
               "OpenOnCreate": true
             },
             "StarterContent": [
               {
                 "AssetUri": "asset:///Content/Materials/Default.omat.json",
                 "RelativePath": "Content/Materials/Default.omat.json",
                 "Kind": "Material"
               }
             ]
           }
           """;

    private static void CreateTemplateSkeleton(MockFileSystem fs, string root)
    {
        foreach (var folder in new[]
                 {
                     "Content",
                     "Content/Scenes",
                     "Content/Materials",
                     "Content/Geometry",
                     "Content/Textures",
                     "Content/Audio",
                     "Content/Video",
                     "Content/Scripts",
                     "Content/Prefabs",
                     "Content/Animations",
                     "Content/SourceMedia",
                     "Content/SourceMedia/Images",
                     "Content/SourceMedia/Audio",
                     "Content/SourceMedia/Video",
                     "Content/SourceMedia/DCC",
                     "Config",
                     "Media",
                 })
        {
            fs.Directory.CreateDirectory(Path.Combine(root, folder));
        }

        fs.File.WriteAllText(Path.Combine(root, "Media", "Icon.png"), "icon");
        fs.File.WriteAllText(Path.Combine(root, "Media", "Preview.png"), "preview");
        fs.File.WriteAllText(
            Path.Combine(root, "Content", "Scenes", "Main.oscene.json"),
            """
            {
              "Id": "09b406b8-410c-4cc3-8c0a-5f6af9afcc0c",
              "Name": "Main",
              "RootNodes": []
            }
            """);
        fs.File.WriteAllText(
            Path.Combine(root, "Content", "Materials", "Default.omat.json"),
            """
            {
              "Schema": "oxygen.material.v1",
              "Type": "PBR",
              "Name": "Default",
              "PbrMetallicRoughness": {
                "BaseColorFactor": [1, 1, 1, 1],
                "MetallicFactor": 0,
                "RoughnessFactor": 0.5
              },
              "AlphaMode": "OPAQUE",
              "DoubleSided": false
            }
            """);
    }

    private static ProjectContext CreateContext(string name)
        => new()
        {
            ProjectId = Guid.NewGuid(),
            Name = name,
            Category = Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };

    private sealed class RecordingObserver<T> : IObserver<T>
    {
        public List<T> Values { get; } = [];

        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
        }

        public void OnNext(T value) => this.Values.Add(value);
    }
}
