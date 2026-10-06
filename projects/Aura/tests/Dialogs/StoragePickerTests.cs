// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Tests;
using Microsoft.UI;
using Moq;

namespace DroidNet.Aura.Tests;

[TestClass]
[TestCategory("UITest")]
public sealed class StoragePickerTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow(@"D:\Projects")]
    [DataRow(@"\\server\share\Projects")]
    public Task FolderPicker_PreservesUnrestrictedStartFolderAsync(string folder) => EnqueueAsync(async () =>
    {
        await LoadTestContentAsync(new Microsoft.UI.Xaml.Controls.Grid()).ConfigureAwait(true);
        var spec = new FolderPickerSpec("Choose project location", "Oxygen.ProjectLocation")
        {
            SuggestedStartFolder = folder,
        };
        var picker = DialogService.CreateFolderPicker(spec, VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        _ = picker.Title.Should().Be(spec.Title);
        _ = picker.SettingsIdentifier.Should().Be(spec.SettingsIdentifier);
        _ = picker.SuggestedStartFolder.Should().Be(folder);
    });

    [TestMethod]
    public Task FilePicker_PreservesLabeledGroupsAndLocationAsync() => EnqueueAsync(async () =>
    {
        await LoadTestContentAsync(new Microsoft.UI.Xaml.Controls.Grid()).ConfigureAwait(true);
        var choices = new Dictionary<string, IList<string>>(StringComparer.Ordinal)
        {
            ["Supported assets"] = [".gltf", ".glb", ".fbx", ".png"],
            ["3D models"] = [".gltf", ".glb", ".fbx"],
            ["Textures"] = [".png"],
        };
        var spec = new FilePickerSpec("Import asset", "Oxygen.ImportAsset", choices)
        {
            SuggestedStartFolder = @"D:\Project Sources",
        };
        var picker = DialogService.CreateFilePicker(spec, VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        _ = picker.Title.Should().Be(spec.Title);
        _ = picker.SettingsIdentifier.Should().Be(spec.SettingsIdentifier);
        _ = picker.SuggestedStartFolder.Should().Be(spec.SuggestedStartFolder);
        _ = picker.FileTypeChoices.Keys.Should().Equal(choices.Keys);
        foreach (var (label, extensions) in choices)
        {
            _ = picker.FileTypeChoices[label].Should().Equal(extensions);
        }
    });

    [TestMethod]
    public async Task Pickers_MissingOwnerAndCancellationAreExplicitAsync()
    {
        var service = new DialogService(Mock.Of<IWindowManagerService>());
        var folder = new FolderPickerSpec("Folder", "Test.Folder");
        var file = new FilePickerSpec("File", "Test.File", new Dictionary<string, IList<string>>(StringComparer.Ordinal) { ["Text"] = [".txt"] });
        Func<Task> pickFolder = () => service.PickFolderAsync(folder, new WindowId(42), this.TestContext.CancellationToken);
        Func<Task> pickFile = () => service.PickFileAsync(file, new WindowId(42), this.TestContext.CancellationToken);
        _ = await pickFolder.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        _ = await pickFile.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(false);
        pickFolder = () => service.PickFolderAsync(folder, cancellation.Token);
        pickFile = () => service.PickFileAsync(file, new WindowId(42), cancellation.Token);
        _ = await pickFolder.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = await pickFile.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Pickers_ExplicitMissingOwnerDoesNotUseActiveWindowAsync()
    {
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.ActiveWindow).Returns(Mock.Of<IManagedWindow>());
        var service = new DialogService(windows.Object);
        var folder = new FolderPickerSpec("Folder", "Test.Folder");
        var file = new FilePickerSpec("File", "Test.File", new Dictionary<string, IList<string>>(StringComparer.Ordinal) { ["Text"] = [".txt"] });
        Func<Task> pickFolder = () => service.PickFolderAsync(folder, new WindowId(42), this.TestContext.CancellationToken);
        Func<Task> pickFile = () => service.PickFileAsync(file, new WindowId(42), this.TestContext.CancellationToken);
        _ = await pickFolder.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        _ = await pickFile.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        windows.VerifyGet(value => value.ActiveWindow, Times.Never());
    }
}
