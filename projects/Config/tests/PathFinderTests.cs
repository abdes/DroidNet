// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using FluentAssertions;
using Moq;

namespace DroidNet.Config.Tests;

/// <summary>
/// Contains unit tests for the <see cref="PathFinder" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("PathFinder")]
public class PathFinderTests
{
    private readonly Mock<IFileSystem> mockFileSystem;

    /// <summary>
    /// Initializes a new instance of the <see cref="PathFinderTests"/> class.
    /// </summary>
    public PathFinderTests()
    {
        this.mockFileSystem = new Mock<IFileSystem>();

        // Mocking file system paths
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>()))
            .Returns<string>(Path.GetFullPath);
        _ = this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldSetMode(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var expectedMode = pathFinder.Mode;

        // Assert
        _ = expectedMode.Should().Be(mode);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldSetApplicationName(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var appName = pathFinder.ApplicationName;

        // Assert
        _ = appName.Should().Be(config.ApplicationName);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetSystemRootPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var systemRoot = pathFinder.SystemRoot;

        // Assert
        _ = systemRoot.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.System));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetTempPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var tempPath = pathFinder.Temp;

        // Assert
        _ = tempPath.Should().Be(Path.GetTempPath());
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetUserDesktopPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var desktopPath = pathFinder.UserDesktop;

        // Assert
        _ = desktopPath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetUserDownloadsPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var downloadsPath = pathFinder.UserDownloads;

        // Assert
        var knownFolderPath = KnownFolderPathHelpers.SHGetKnownFolderPath(
            new Guid("374DE290-123F-4565-9164-39C4925E467B"),
            0);
        _ = downloadsPath.Should().Be(knownFolderPath);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetUserHomePath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var homePath = pathFinder.UserHome;

        // Assert
        _ = homePath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetUserDocumentsPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var documentsPath = pathFinder.UserDocuments;

        // Assert
        _ = documentsPath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.Personal));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetProgramDataPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        var expectedPath = AppContext.BaseDirectory;

        // Act
        var programDataPath = pathFinder.ProgramData;

        // Assert
        _ = programDataPath.Should().Be(expectedPath);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetLocalAppDataPath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        string expectedPath;
        if (string.Equals(mode, PathFinder.RealMode, StringComparison.Ordinal))
        {
            var localAppDataSpecialFolder = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            expectedPath = Path.Combine(localAppDataSpecialFolder, config.CompanyName, config.ApplicationName);
        }
        else
        {
            var localAppData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                config.CompanyName,
                config.ApplicationName);
            expectedPath = Path.Combine(localAppData, "Development");
        }

        // Act
        var localAppDataPath = pathFinder.LocalAppData;

        // Assert
        _ = localAppDataPath.Should().Be(expectedPath);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetLocalAppStatePath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var localAppStatePath = pathFinder.LocalAppState;

        // Assert
        _ = localAppStatePath.Should().Be(Path.Combine(pathFinder.LocalAppData, ".state"));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetConfigFilePath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        _ = this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);

        const string configFileName = "config.json";

        // Act
        var configFilePath = pathFinder.GetConfigFilePath(configFileName);

        // Assert
        _ = configFilePath.Should().Be(Path.Combine(pathFinder.LocalAppData, configFileName));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinderShouldGetProgramConfigFilePath(string mode)
    {
        // Arrange
        var config = new PathFinderConfig(mode, "MyCompany", "MyApp");
        _ = this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        _ = this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);

        const string configFileName = "programConfig.json";

        // Act
        var configFilePath = pathFinder.GetProgramConfigFilePath(configFileName);

        // Assert
        _ = configFilePath.Should().Be(Path.Combine(pathFinder.ProgramData, configFileName));
    }
}
