// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using DroidNet.Config;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Core.Services;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Oxygen PathFinder")]
public class OxygenPathFinderTests
{
    private readonly Mock<IPathFinder> mockPathFinder;
    private readonly Mock<IFileSystem> mockFileSystem;

    public OxygenPathFinderTests()
    {
        this.mockPathFinder = new Mock<IPathFinder>();
        this.mockFileSystem = new Mock<IFileSystem>();

        _ = this.mockPathFinder.Setup(pf => pf.UserDocuments).Returns(@"C:\Users\TestUser\Documents");
        _ = this.mockPathFinder.Setup(pf => pf.LocalAppData).Returns(@"C:\Users\TestUser\AppData\Local\TestApp");
        _ = this.mockPathFinder.Setup(pf => pf.Temp).Returns(@"C:\Temp");
        _ = this.mockPathFinder.Setup(pf => pf.LocalAppState).Returns(@"C:\Users\TestUser\AppData\Local\TestApp\.state");
        _ = this.mockPathFinder.Setup(pf => pf.ProgramData).Returns(@"C:\ProgramData\TestApp");
        _ = this.mockPathFinder.Setup(pf => pf.GetConfigFilePath(It.IsAny<string>())).Returns<string>(fileName => $@"C:\Users\TestUser\AppData\Local\TestApp\{fileName}");
        _ = this.mockPathFinder.Setup(pf => pf.GetProgramConfigFilePath(It.IsAny<string>())).Returns<string>(fileName => $@"C:\ProgramData\TestApp\{fileName}");

        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        _ = this.mockFileSystem.Setup(fs => fs.Directory.CreateDirectory(It.IsAny<string>())).Returns<string>(_ => new Mock<IDirectoryInfo>().Object);
    }

    [TestMethod]
    public void PersonalProjectsShouldReturnCorrectPath()
    {
        // Arrange
        const string expectedPath = @"C:\Users\TestUser\Documents\Oxygen Projects";
        var sut = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Act
        var actualPath = sut.PersonalProjects;

        // Assert
        _ = actualPath.Should().Be(expectedPath);
    }

    [TestMethod]
    public void LocalProjectsShouldReturnCorrectPath()
    {
        // Arrange
        const string expectedPath = @"C:\Users\TestUser\AppData\Local\TestApp\Oxygen Projects";
        var sut = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Act
        var actualPath = sut.LocalProjects;

        // Assert
        _ = actualPath.Should().Be(expectedPath);
    }

    [TestMethod]
    public void ConstructorShouldCreateRequiredDirectories()
    {
        // Act
        _ = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Assert
        this.mockFileSystem.Verify(fs => fs.Directory.CreateDirectory(@"C:\Temp"), Times.Once);
        this.mockFileSystem.Verify(fs => fs.Directory.CreateDirectory(@"C:\Users\TestUser\AppData\Local\TestApp"), Times.Once);
        this.mockFileSystem.Verify(fs => fs.Directory.CreateDirectory(@"C:\Users\TestUser\AppData\Local\TestApp\.state"), Times.Once);
        this.mockFileSystem.Verify(fs => fs.Directory.CreateDirectory(@"C:\Users\TestUser\AppData\Local\TestApp\Oxygen Projects"), Times.Once);
        this.mockFileSystem.Verify(fs => fs.Directory.CreateDirectory(@"C:\Users\TestUser\Documents\Oxygen Projects"), Times.Once);
    }

    [TestMethod]
    public void GetConfigFilePathShouldReturnCorrectPath()
    {
        // Arrange
        const string configFileName = "config.json";
        const string expectedPath = @"C:\Users\TestUser\AppData\Local\TestApp\config.json";
        var sut = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Act
        var actualPath = sut.GetConfigFilePath(configFileName);

        // Assert
        _ = actualPath.Should().Be(expectedPath);
    }

    [TestMethod]
    public void GetProgramConfigFilePathShouldReturnCorrectPath()
    {
        // Arrange
        const string configFileName = "programConfig.json";
        const string expectedPath = @"C:\ProgramData\TestApp\programConfig.json";
        var sut = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Act
        var actualPath = sut.GetProgramConfigFilePath(configFileName);

        // Assert
        _ = actualPath.Should().Be(expectedPath);
    }

    [TestMethod]
    public void StateDatabasePathShouldReturnCorrectPath()
    {
        // Arrange
        const string expectedPath = @"C:\Users\TestUser\AppData\Local\TestApp\.state\state.db";
        var sut = new OxygenPathFinder(this.mockPathFinder.Object, this.mockFileSystem.Object);

        // Act
        var actualPath = sut.StateDatabasePath;

        // Assert
        _ = actualPath.Should().Be(expectedPath);
    }
}
