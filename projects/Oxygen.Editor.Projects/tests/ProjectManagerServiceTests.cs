// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.TestHelpers;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ProjectManagerService")]
public partial class ProjectManagerServiceTests : TestSuiteWithAssertions
{
    private readonly Mock<ILogger<ProjectManagerService>> mockLogger;
    private readonly Mock<IStorageProvider> mockStorage;
    private readonly ProjectManagerService projectManagerService;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ProjectManagerServiceTests" /> class.
    /// </summary>
    public ProjectManagerServiceTests()
    {
        this.mockStorage = new Mock<IStorageProvider>();
        this.mockLogger = new Mock<ILogger<ProjectManagerService>>();
        var mockLoggerFactory = new Mock<ILoggerFactory>();
        _ = mockLoggerFactory.Setup(x => x.CreateLogger(It.IsAny<string>())).Returns(() => this.mockLogger.Object);

        this.projectManagerService = new ProjectManagerService(
            this.mockStorage.Object,
            mockLoggerFactory.Object);
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldReturnProjectInfo_WhenProjectExists()
    {
        // Arrange
        const string projectFolderPath = "valid/path";
        var projectFolderMock = new Mock<IFolder>();
        var projectFileMock = new Mock<IDocument>();
        var projectInfo = new ProjectInfo("name", Category.Games, projectFolderPath, "Media/Preview.png");
        var json = /*lang=json,strict*/
            $$"""
              {
                "Id": "{{projectInfo.Id}}",
                "Name": "name",
                "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
                "Thumbnail": "Media/Preview.png"
              }
              """;

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetDocumentAsync(Constants.ProjectFileName, CancellationToken.None))
            .ReturnsAsync(projectFileMock.Object);
        _ = projectFileMock.Setup(f => f.ReadAllTextAsync(CancellationToken.None))
            .ReturnsAsync(json);

        // Act
        var result = await this.projectManagerService.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result.Should().BeEquivalentTo(projectInfo, opts => opts
            .Excluding(pi => pi.LastUsedOn)
            .Excluding(pi => pi.Id));
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldReturnNull_WhenProjectDoesNotExist()
    {
        // Arrange
        const string projectFolderPath = "invalid/path";
        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ThrowsAsync(new DirectoryNotFoundException());

        // Act
        var result = await this.projectManagerService.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldLogError_WhenExceptionIsThrown()
    {
        // Arrange
        const string projectFolderPath = "path/with/error";
        const string exceptionMessage = "Some error";
        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ThrowsAsync(new InvalidOperationException(exceptionMessage));

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeNull();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public async Task SaveProjectInfoAsync_ShouldReturnTrue_WhenProjectInfoIsSavedSuccessfully()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var documentMock = new Mock<IDocument>();
        _ = this.mockStorage.Setup(s => s.GetDocumentFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(documentMock.Object);

        // Act
        var result = await this.projectManagerService.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

        // Assert
        // Assert
        _ = result.Should().BeTrue();
        var expectedJson = ProjectInfo.ToJson(projectInfo);
        documentMock.Verify(d => d.WriteAllTextAsync(expectedJson, It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task SaveProjectInfoAsync_ShouldReturnFalse_WhenExceptionIsThrown()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "path/with/error", "Media/Preview.png");
        const string exceptionMessage = "Some error";
        _ = this.mockStorage.Setup(s => s.NormalizeRelativeTo(projectInfo.Location!, Constants.ProjectFileName))
            .Returns("normalized/path");
        _ = this.mockStorage.Setup(s => s.GetDocumentFromPathAsync("normalized/path", CancellationToken.None))
            .ThrowsAsync(new InvalidOperationException(exceptionMessage));

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public async Task LoadProjectAsync_ShouldReturnTrue_WhenProjectIsLoadedSuccessfully()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var projectFolderMock = new Mock<IFolder>();
        var scenesFolderMock = new Mock<IFolder>();
        var sceneDocumentMock = new Mock<IDocument>();

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetFolderAsync(Constants.ScenesFolderName, CancellationToken.None))
            .ReturnsAsync(scenesFolderMock.Object);
        _ = scenesFolderMock.Setup(f => f.GetDocumentsAsync(It.IsAny<CancellationToken>()))
            .Returns(new List<IDocument> { sceneDocumentMock.Object }.ToAsyncEnumerable());
        _ = scenesFolderMock.Setup(d => d.ExistsAsync()).ReturnsAsync(value: true);
        _ = sceneDocumentMock.Setup(d => d.Name).Returns("scene1.scene");

        // Provide valid scene JSON so Scene.FromJson can deserialize and be added to the project
        const string sceneJson = /*lang=json,strict*/
            """
            {
              "Name": "scene1",
              "Nodes": []
            }
            """;
        _ = sceneDocumentMock.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>())).ReturnsAsync(sceneJson);
        _ = sceneDocumentMock.Setup(d => d.ExistsAsync()).ReturnsAsync(value: true);

        // Act
        var result = await this.projectManagerService.LoadProjectAsync(projectInfo).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
        _ = this.projectManagerService.CurrentProject.Should().NotBeNull();
        _ = this.projectManagerService.CurrentProject!.Scenes.Should().ContainSingle();
        _ = this.projectManagerService.CurrentProject.Scenes[0].Name.Should().Be("scene1");
    }

    [TestMethod]
    public async Task LoadProjectAsync_ShouldReturnFalse_WhenProjectLocationIsNull()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, location: null, "Media/Preview.png");

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadProjectAsync(projectInfo).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#else
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
#endif
    }

    [TestMethod]
    public async Task LoadProjectAsync_ShouldReturnFalse_WhenProjectScenesFailToLoad()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var projectFolderMock = new Mock<IFolder>();

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetFolderAsync(Constants.ScenesFolderName, CancellationToken.None))
            .ThrowsAsync(new InvalidOperationException("exception generated by test case"));

        // Act
        var result = await this.projectManagerService.LoadProjectAsync(projectInfo).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        _ = this.projectManagerService.CurrentProject.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnTrue_WhenSceneIsLoadedSuccessfully()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };
        const string sceneJson =
            """
            {
                "Name": "scene",
                "Nodes": [
                    {
                        "Name": "node1"
                    }
                ]
            }
            """;
        var documentMock = new Mock<IDocument>();
        var projectFolderMock = new Mock<IFolder>();
        var scenesFolderMock = new Mock<IFolder>();

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetFolderAsync(Constants.ScenesFolderName, CancellationToken.None))
            .ReturnsAsync(scenesFolderMock.Object);
        _ = scenesFolderMock.Setup(f => f.GetDocumentAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(documentMock.Object);
        _ = documentMock.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>()))
            .ReturnsAsync(sceneJson);
        _ = documentMock.Setup(d => d.ExistsAsync()).ReturnsAsync(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
        _ = scene.Nodes.Should().ContainSingle();
        _ = scene.Nodes.ElementAt(0).Name.Should().Be("node1");
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnFalse_WhenSceneFileDoesNotExist()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };
        var documentMock = new Mock<IDocument>();
        var projectFolderMock = new Mock<IFolder>();
        var scenesFolderMock = new Mock<IFolder>();

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetFolderAsync(Constants.ScenesFolderName, CancellationToken.None))
            .ReturnsAsync(scenesFolderMock.Object);
        _ = scenesFolderMock.Setup(f => f.GetDocumentAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(documentMock.Object);
        _ = documentMock.Setup(d => d.ExistsAsync()).ReturnsAsync(value: false);

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnFalse_WhenSceneFileContainsInvalidJson()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };
        const string invalidJson = "";
        var documentMock = new Mock<IDocument>();
        var projectFolderMock = new Mock<IFolder>();
        var scenesFolderMock = new Mock<IFolder>();

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        _ = projectFolderMock.Setup(f => f.GetFolderAsync(Constants.ScenesFolderName, CancellationToken.None))
            .ReturnsAsync(scenesFolderMock.Object);
        _ = scenesFolderMock.Setup(f => f.GetDocumentAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(documentMock.Object);
        _ = documentMock.Setup(d => d.ExistsAsync()).ReturnsAsync(value: true);
        _ = documentMock.Setup(d => d.ReadAllTextAsync(It.IsAny<CancellationToken>())).ReturnsAsync(invalidJson);

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnFalse_WhenProjectLocationIsNull()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, location: null, "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#else
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
#endif
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnFalse_WhenProjectLocationDoesNotExist()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "invalid/path", "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };

        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ThrowsAsync(new DirectoryNotFoundException());

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public async Task LoadSceneAsync_ShouldReturnFalse_WhenExceptionIsThrown()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "path/with/error", "Media/Preview.png");
        var project = new Project(projectInfo) { Name = projectInfo.Name };
        var scene = new Scene(project) { Name = "scene" };
        const string exceptionMessage = "Some error";
        _ = this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ThrowsAsync(new InvalidOperationException(exceptionMessage));

        _ = this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.projectManagerService.LoadSceneAsync(scene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

    [TestMethod]
    public void GetCurrentProjectStorageProvider_ShouldReturnStorageProvider()
    {
        // Act
        var result = this.projectManagerService.GetCurrentProjectStorageProvider();

        // Assert
        _ = result.Should().Be(this.mockStorage.Object);
    }

    [TestMethod]
    public void Ctor_DefaultGeneratesNonEmptyId()
    {
        var pi = new ProjectInfo("name", Category.Games, "loc", "thumb");
        _ = pi.Id.Should().NotBe(Guid.Empty);
    }

    [TestMethod]
    public void Ctor_WithExplicitId_PreservesId()
    {
        var id = Guid.NewGuid();
        var pi = new ProjectInfo(id, "name", Category.Games);
        _ = pi.Id.Should().Be(id);
    }

    [TestMethod]
    public void Ctor_WithEmptyId_ThrowsArgumentException()
    {
        Action act = () => _ = new ProjectInfo(Guid.Empty, "name", Category.Games);
        _ = act.Should().Throw<ArgumentException>().WithMessage("*Project Id*");
    }

    [TestMethod]
    public void FromJson_ThrowsJsonException_WhenIdMissing()
    {
        const string json = /*lang=json,strict*/
            """
            {
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C"
            }
            """;

        Action act = () => _ = ProjectInfo.FromJson(json);
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void FromJson_ThrowsJsonException_WhenIdEmpty()
    {
        const string json = /*lang=json,strict*/
            """
            {
              "Id": "",
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C"
            }
            """;

        Action act = () => _ = ProjectInfo.FromJson(json);
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void ToJsonAndFromJson_PreservesId()
    {
        var pi = new ProjectInfo("name", Category.Games, "loc", "thumb");
        var json = ProjectInfo.ToJson(pi);
        var des = ProjectInfo.FromJson(json);
        _ = des.Id.Should().Be(pi.Id);
    }
}
