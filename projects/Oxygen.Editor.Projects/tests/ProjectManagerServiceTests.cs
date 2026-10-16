// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.TestHelpers;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.World;

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
        projectInfo.AuthoringMounts.Add(new ProjectMountPoint(Name: "Content", RelativePath: "Content"));
        var json = /*lang=json,strict*/
            $$"""
              {
                "SchemaVersion": 1,
                "Id": "{{projectInfo.Id}}",
                "Name": "name",
                "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
                "Thumbnail": "Media/Preview.png",
                "AuthoringMounts": [
                    {
                        "Name": "Content",
                        "RelativePath": "Content"
                    }
                ]
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
        var atomic = new Mock<IAtomicFileStore>();
        _ = this.mockStorage.SetupGet(value => value.AtomicFiles).Returns(atomic.Object);
        _ = this.mockStorage.Setup(value => value.NormalizeRelativeTo(projectInfo.Location!, Constants.ProjectFileName)).Returns("valid/path/Project.oxy");
        _ = atomic.Setup(value => value.ReadAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync(new FileSnapshot([], FileVersion.Missing));
        _ = atomic.Setup(value => value.WriteAsync(It.IsAny<string>(), It.IsAny<ReadOnlyMemory<byte>>(), FileVersion.Missing, It.IsAny<CancellationToken>())).ReturnsAsync(new FileVersion(Exists: true, "saved"));

        // Act
        var result = await this.projectManagerService.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

        // Assert
        // Assert
        _ = result.Should().BeTrue();
        var expectedJson = ProjectInfo.ToJson(projectInfo);
        atomic.Verify(value => value.WriteAsync("valid/path/Project.oxy", It.Is<ReadOnlyMemory<byte>>(bytes => System.Text.Encoding.UTF8.GetString(bytes.ToArray()) == expectedJson), FileVersion.Missing, It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task SaveProjectInfoAsync_ShouldReturnFalse_WhenExceptionIsThrown()
    {
        // Arrange
        var projectInfo = new ProjectInfo("name", Category.Games, "path/with/error", "Media/Preview.png");
        const string exceptionMessage = "Some error";
        _ = this.mockStorage.Setup(s => s.NormalizeRelativeTo(projectInfo.Location!, Constants.ProjectFileName))
            .Returns("normalized/path");
        var atomic = new Mock<IAtomicFileStore>();
        _ = this.mockStorage.SetupGet(value => value.AtomicFiles).Returns(atomic.Object);
        _ = atomic.Setup(s => s.ReadAsync("normalized/path", CancellationToken.None))
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
        // Arrange: a project's scenes are its scene files, which loading the project does not read.
        var projectInfo = new ProjectInfo("name", Category.Games, "valid/path", "Media/Preview.png");

        // Act
        var result = await this.projectManagerService.LoadProjectAsync(projectInfo).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
        _ = this.projectManagerService.CurrentProject.Should().NotBeNull();
        _ = this.projectManagerService.CurrentProject!.ProjectInfo.Should().BeSameAs(projectInfo);
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
              "SchemaVersion": 1,
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C"
            }
            """;

        Action act = () => _ = ProjectInfo.FromJson(json);
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void FromJson_ThrowsJsonException_WhenSchemaVersionMissing()
    {
        const string json = /*lang=json,strict*/
            """
            {
              "Id": "2F8B4E5F-754E-4A4F-B4B4-4D40BD2E5E32",
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C"
            }
            """;

        Action act = () => _ = ProjectInfo.FromJson(json);
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void FromJson_ThrowsUnsupportedProjectSchemaException_WhenSchemaVersionUnsupported()
    {
        const string json = /*lang=json,strict*/
            """
            {
              "SchemaVersion": 999,
              "Id": "2F8B4E5F-754E-4A4F-B4B4-4D40BD2E5E32",
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C"
            }
            """;

        Action act = () => _ = ProjectInfo.FromJson(json);
        _ = act.Should().Throw<UnsupportedProjectSchemaException>();
    }

    [TestMethod]
    public void FromJson_ThrowsJsonException_WhenIdEmpty()
    {
        const string json = /*lang=json,strict*/
            """
            {
              "SchemaVersion": 1,
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

    [TestMethod]
    public void ToJson_ShouldWriteSchemaVersionAndIgnoreRuntimeFields()
    {
        var pi = new ProjectInfo("name", Category.Games, "loc", "thumb")
        {
            LastUsedOn = new DateTime(year: 2024, month: 1, day: 2),
        };

        var json = ProjectInfo.ToJson(pi);

        _ = json.Should().Contain("\"SchemaVersion\": 1");
        _ = json.Should().NotContain(nameof(ProjectInfo.Location));
        _ = json.Should().NotContain(nameof(ProjectInfo.LastUsedOn));
    }
}
