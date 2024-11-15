// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Tests.Native;

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Config;
using Oxygen.Editor.Projects.Storage;
using Oxygen.Editor.Projects.Utils;
using Oxygen.Editor.Storage;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("NativeProjectSource")]
public class LocalProjectSourceTests
{
    private static readonly ProjectCategory SampleCategory = new(
        "C44E7604-B265-40D8-9442-11A01ECE334C",
        "Category",
        "Description");

    private readonly Mock<IStorageProvider> mockStorage;
    private readonly Mock<ILogger<LocalProjectsSource>> mockLogger;
    private readonly LocalProjectsSource localProjectsSource;
    private readonly JsonSerializerOptions jsonOptions;

    public LocalProjectSourceTests()
    {
        this.mockStorage = new Mock<IStorageProvider>();
        this.mockLogger = new Mock<ILogger<LocalProjectsSource>>();
        var mockSettings = new Mock<IOptions<ProjectsSettings>>();
        mockSettings.Setup(s => s.Value)
            .Returns(
                new ProjectsSettings()
                {
                    Categories = [SampleCategory],
                });

        this.jsonOptions = new JsonSerializerOptions
        {
            AllowTrailingCommas = true,
            Converters = { new CategoryJsonConverter(mockSettings.Object.Value) },
            WriteIndented = true,
        };

        this.localProjectsSource = new LocalProjectsSource(
            this.mockStorage.Object,
            this.mockLogger.Object,
            mockSettings.Object);
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldReturnProjectInfo_WhenProjectExists()
    {
        // Arrange
        const string projectFolderPath = "valid/path";
        var projectFolderMock = new Mock<IFolder>();
        var projectFileMock = new Mock<IDocument>();
        var projectInfo = new ProjectInfo
        {
            Name = "name",
            Category = SampleCategory,
            Thumbnail = "Media/Preview.png",
            Location = projectFolderPath,
        };
        const string json = /*lang=json,strict*/
            """
            {
              "Name": "name",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
              "Thumbnail": "Media/Preview.png"
            }
            """;

        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ReturnsAsync(projectFolderMock.Object);
        projectFolderMock.Setup(f => f.GetDocumentAsync(Constants.ProjectFileName, CancellationToken.None))
            .ReturnsAsync(projectFileMock.Object);
        projectFileMock.Setup(f => f.ReadAllTextAsync(CancellationToken.None))
            .ReturnsAsync(json);

        // Act
        var result = await this.localProjectsSource.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        result.Should().NotBeNull();
        result.Should().BeEquivalentTo(projectInfo, opts => opts.Excluding(pi => pi.LastUsedOn));
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldReturnNull_WhenProjectDoesNotExist()
    {
        // Arrange
        const string projectFolderPath = "invalid/path";
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ThrowsAsync(new DirectoryNotFoundException());

        // Act
        var result = await this.localProjectsSource.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        result.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadProjectInfoAsync_ShouldLogError_WhenExceptionIsThrown()
    {
        // Arrange
        const string projectFolderPath = "path/with/error";
        const string exceptionMessage = "Some error";
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(projectFolderPath, CancellationToken.None))
            .ThrowsAsync(exception: new InvalidOperationException(exceptionMessage));

        this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.localProjectsSource.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

        // Assert
        result.Should().BeNull();
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
        var projectInfo = new ProjectInfo
        {
            Location = "valid/path",
            Name = "name",
            Category = SampleCategory,
            Thumbnail = "Media/Preview.png",
        };
        var json = JsonSerializer.Serialize(projectInfo, this.jsonOptions);
        var documentMock = new Mock<IDocument>();

        this.mockStorage.Setup(s => s.NormalizeRelativeTo(projectInfo.Location, Constants.ProjectFileName))
            .Returns("normalized/path");
        this.mockStorage.Setup(s => s.GetDocumentFromPathAsync("normalized/path", CancellationToken.None))
            .ReturnsAsync(documentMock.Object);
        documentMock.Setup(d => d.WriteAllTextAsync(json, CancellationToken.None))
            .Returns(Task.CompletedTask);

        // Act
        var result = await this.localProjectsSource.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

        // Assert
        result.Should().BeTrue();
    }

    [TestMethod]
    public async Task SaveProjectInfoAsync_ShouldReturnFalse_WhenExceptionIsThrown()
    {
        // Arrange
        var projectInfo = new ProjectInfo
        {
            Location = "path/with/error",
            Name = "name",
            Category = SampleCategory,
            Thumbnail = "Media/Preview.png",
        };
        const string exceptionMessage = "Some error";
        this.mockStorage.Setup(s => s.NormalizeRelativeTo(projectInfo.Location, Constants.ProjectFileName))
            .Returns("normalized/path");
        this.mockStorage.Setup(s => s.GetDocumentFromPathAsync("normalized/path", CancellationToken.None))
            .ThrowsAsync(new InvalidOperationException(exceptionMessage));

        this.mockLogger.Setup(x => x.IsEnabled(LogLevel.Error)).Returns(value: true);

        // Act
        var result = await this.localProjectsSource.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

        // Assert
        result.Should().BeFalse();
        this.mockLogger.Verify(
            x => x.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.IsAny<Exception>(),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once);
    }

#if false
    [TestMethod]
    public async Task CanCreateProjectAsync_ContainingFolderDoesNotExist_ReturnsFalse()
    {
        // Arrange
        this.mockStorage.Setup(s => s.FolderExistsAsync(It.IsAny<string>())).ReturnsAsync(value: false);

        // Act
        var result = await this.localProjectsSource.CanCreateProjectAsync("ProjectName", "InvalidPath");

        // Assert
        result.Should().BeFalse();
    }

    [TestMethod]
    public async Task CanCreateProjectAsync_ProjectFolderExistsAndIsNotEmpty_ReturnsFalse()
    {
        // Arrange
        this.mockStorage.Setup(s => s.FolderExistsAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(new Mock<IFolder>().Object);
        var folderMock = new Mock<IFolder>();
        folderMock.Setup(f => f.ExistsAsync()).ReturnsAsync(value: true);
        folderMock.Setup(f => f.HasItemsAsync()).ReturnsAsync(value: true);
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(folderMock.Object);

        // Act
        var result = await this.localProjectsSource.CanCreateProjectAsync("ProjectName", "ValidPath");

        // Assert
        result.Should().BeFalse();
    }

    [TestMethod]
    public async Task CanCreateProjectAsync_ProjectFolderDoesNotExist_ReturnsTrue()
    {
        // Arrange
        this.mockStorage.Setup(s => s.FolderExistsAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var folderMock = new Mock<IFolder>();
        folderMock.Setup(f => f.ExistsAsync()).ReturnsAsync(value: false);
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(folderMock.Object);

        // Act
        var result = await this.localProjectsSource.CanCreateProjectAsync("ProjectName", "ValidPath");

        // Assert
        result.Should().BeTrue();
    }

    [TestMethod]
    public async Task CanCreateProjectAsync_ProjectFolderExistsAndIsEmpty_ReturnsTrue()
    {
        // Arrange
        this.mockStorage.Setup(s => s.FolderExistsAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var folderMock = new Mock<IFolder>();
        folderMock.Setup(f => f.ExistsAsync()).ReturnsAsync(value: true);
        folderMock.Setup(f => f.HasItemsAsync()).ReturnsAsync(value: false);
        this.mockStorage.Setup(s => s.GetFolderFromPathAsync(It.IsAny<string>(), CancellationToken.None))
            .ReturnsAsync(folderMock.Object);

        // Act
        var result = await this.localProjectsSource.CanCreateProjectAsync("ProjectName", "ValidPath");

        // Assert
        result.Should().BeTrue();
    }
#endif
}
