// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Services;
using Testably.Abstractions;

namespace Oxygen.Editor.ProjectBrowser.Tests;

[TestClass]
public sealed class ProjectLocationTests
{
    [TestMethod]
    [DataRow("")]
    [DataRow(@"D:\Projects")]
    [DataRow(@"\\server\share\Projects")]
    public void NewProject_PreservesLastLocationOrDefaultsToAppData(string lastLocation)
    {
        const string local = @"C:\Users\Test\AppData\Local\DroidNet\Oxygen Editor\Oxygen Projects";
        const string personal = @"C:\Users\Test\Documents\Oxygen Projects";
        var finder = new Mock<IOxygenPathFinder>();
        _ = finder.SetupGet(value => value.LocalProjects).Returns(local);
        _ = finder.SetupGet(value => value.PersonalProjects).Returns(personal);
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.GetDescriptorsByCategory())
            .Returns(new Dictionary<string, IReadOnlyList<ISettingDescriptor>>(StringComparer.Ordinal)
            {
                ["Recent"] = [Mock.Of<ISettingDescriptor>(
                    value => value.Name == nameof(ProjectBrowserSettings.LastSaveLocation))],
            });
        _ = settings.Setup(value => value.LoadSettingOrDefaultAsync(
                It.IsAny<SettingKey<string>>(),
                It.IsAny<string>(),
                It.IsAny<SettingContext?>(),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()))
            .Returns<SettingKey<string>, string, SettingContext?, IProgress<SettingsProgress>?, CancellationToken>(
                (key, defaultValue, _, _, _) => Task.FromResult(
                    string.Equals(key.Name, nameof(ProjectBrowserSettings.LastSaveLocation), StringComparison.Ordinal)
                        ? lastLocation : defaultValue));
        var browser = new ProjectBrowserService(
            Mock.Of<IProjectCreationService>(),
            Mock.Of<IRecentProjectAdapter>(),
            finder.Object,
            new NativeStorageProvider(new RealFileSystem()),
            settings.Object);
        using var dialog = new NewProjectDialogViewModel(browser, Mock.Of<ITemplateInfo>());
        _ = dialog.SelectedLocation.Path.Should().Be(string.IsNullOrEmpty(lastLocation) ? local : lastLocation);
        var locations = browser.GetQuickSaveLocations();
        _ = locations[^2].Path.Should().Be(local);
        _ = locations[^1].Path.Should().Be(personal);

        dialog.SetLocationCommand.Execute(new QuickSaveLocation("Custom", @"E:\Other Folder\Projects"));
        _ = dialog.SelectedLocation.Path.Should().Be(@"E:\Other Folder\Projects");
    }
}
