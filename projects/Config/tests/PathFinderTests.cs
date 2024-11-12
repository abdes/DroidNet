// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Tests;

using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Abstractions;
using System.Runtime.InteropServices;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("PathFinder")]
public class PathFinderTests
{
    private readonly Mock<IFileSystem> mockFileSystem;

    public PathFinderTests()
    {
        this.mockFileSystem = new Mock<IFileSystem>();

        // Mocking file system paths
        this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>()))
            .Returns<string>(Path.GetFullPath);
        this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Set_Mode(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var expectedMode = pathFinder.Mode;

        // Assert
        expectedMode.Should().Be(mode);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Set_Application_Name(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var appName = pathFinder.ApplicationName;

        // Assert
        appName.Should().Be(config.ApplicationName);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_System_Root_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var systemRoot = pathFinder.SystemRoot;

        // Assert
        systemRoot.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.System));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Temp_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var tempPath = pathFinder.Temp;

        // Assert
        tempPath.Should().Be(Path.GetTempPath());
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_User_Desktop_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var desktopPath = pathFinder.UserDesktop;

        // Assert
        desktopPath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_User_Downloads_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var downloadsPath = pathFinder.UserDownloads;

        // Assert
        downloadsPath.Should().Be(SHGetKnownFolderPath(new Guid("374DE290-123F-4565-9164-39C4925E467B"), 0));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_User_Home_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var homePath = pathFinder.UserHome;

        // Assert
        homePath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_User_Documents_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var documentsPath = pathFinder.UserDocuments;

        // Assert
        documentsPath.Should().Be(Environment.GetFolderPath(Environment.SpecialFolder.Personal));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Program_Data_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        var expectedPath = AppContext.BaseDirectory;

        // Act
        var programDataPath = pathFinder.ProgramData;

        // Assert
        programDataPath.Should().Be(expectedPath);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Local_App_Data_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
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
        localAppDataPath.Should().Be(expectedPath);
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Local_App_State_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);

        // Act
        var localAppStatePath = pathFinder.LocalAppState;

        // Assert
        localAppStatePath.Should().Be(Path.Combine(pathFinder.LocalAppData, ".state"));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Config_File_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);

        const string configFileName = "config.json";

        // Act
        var configFilePath = pathFinder.GetConfigFilePath(configFileName);

        // Assert
        configFilePath.Should().Be(Path.Combine(pathFinder.LocalAppData, configFileName));
    }

    [TestMethod]
    [DataRow(PathFinder.DevelopmentMode)]
    [DataRow(PathFinder.RealMode)]
    public void PathFinder_Should_Get_Program_Config_File_Path(string mode)
    {
        // Arrange
        var config = new PathFinder.Config(mode, "MyCompany", "MyApp");
        this.mockFileSystem.Setup(fs => fs.Path.GetFullPath(It.IsAny<string>())).Returns<string>(p => p);
        var pathFinder = new PathFinder(this.mockFileSystem.Object, config);
        this.mockFileSystem.Setup(fs => fs.Path.Combine(It.IsAny<string>(), It.IsAny<string>()))
            .Returns<string, string>(Path.Combine);

        const string configFileName = "programConfig.json";

        // Act
        var configFilePath = pathFinder.GetProgramConfigFilePath(configFileName);

        // Assert
        configFilePath.Should().Be(Path.Combine(pathFinder.ProgramData, configFileName));
    }

#pragma warning disable SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, PreserveSig = false)]
    private static extern string SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid refToGuid,
        uint dwFlags,
        nint hToken = default);
#pragma warning restore SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
}
