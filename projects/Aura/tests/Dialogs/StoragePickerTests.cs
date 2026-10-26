// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.Windows.Storage.Pickers;
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
        var choices = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
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
    public Task SavePicker_PreservesNameGroupsAndLocationAsync() => EnqueueAsync(async () =>
    {
        await LoadTestContentAsync(new Microsoft.UI.Xaml.Controls.Grid()).ConfigureAwait(true);
        var choices = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
        {
            ["Import session"] = [".json"],
        };
        var spec = new FileSavePickerSpec("Save import session", "Oxygen.ImportSession", choices)
        {
            SuggestedStartFolder = @"D:\Project",
            SuggestedFileName = "Vehicles.import-manifest.json",
        };
        var picker = DialogService.CreateSavePicker(spec, VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);
        _ = picker.Title.Should().Be(spec.Title);
        _ = picker.SettingsIdentifier.Should().Be(spec.SettingsIdentifier);
        _ = picker.SuggestedStartFolder.Should().Be(spec.SuggestedStartFolder);
        _ = picker.SuggestedFileName.Should().Be(spec.SuggestedFileName);
        _ = picker.FileTypeChoices.Keys.Should().Equal(choices.Keys);
        _ = picker.FileTypeChoices["Import session"].Should().Equal(choices["Import session"]);
    });

    [TestMethod]
    public Task Pickers_WithoutSuggestedStartFolder_KeepThePickerDefaultAsync() => EnqueueAsync(async () =>
    {
        await LoadTestContentAsync(new Microsoft.UI.Xaml.Controls.Grid()).ConfigureAwait(true);
        var owner = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id;
        var choices = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal) { ["Text"] = [".txt"] };

        var folder = DialogService.CreateFolderPicker(new FolderPickerSpec("Folder", "Test.Folder"), owner);
        var file = DialogService.CreateFilePicker(new FilePickerSpec("File", "Test.File", choices), owner);
        var save = DialogService.CreateSavePicker(new FileSavePickerSpec("Save", "Test.Save", choices), owner);

        _ = folder.SuggestedStartFolder.Should().Be(new FolderPicker(owner).SuggestedStartFolder, "an unset start folder leaves the remembered location in charge");
        _ = file.SuggestedStartFolder.Should().Be(new FileOpenPicker(owner).SuggestedStartFolder);
        _ = save.SuggestedStartFolder.Should().Be(new FileSavePicker(owner).SuggestedStartFolder);
    });

    [TestMethod]
    public Task SavePicker_FirstExtensionOfTheFirstChoiceIsTheDefaultAsync() => EnqueueAsync(async () =>
    {
        await LoadTestContentAsync(new Microsoft.UI.Xaml.Controls.Grid()).ConfigureAwait(true);
        var choices = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
        {
            ["Import session"] = ["json", ".manifest"],
            ["Text"] = [".txt"],
        };

        var picker = DialogService.CreateSavePicker(new FileSavePickerSpec("Save", "Test.Save", choices), VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id);

        _ = picker.DefaultFileExtension.Should().Be(".json");
        _ = picker.FileTypeChoices["Import session"].Should().Equal(".json", ".manifest");
    });

    [TestMethod]
    [DataRow(".png", ".png")]
    [DataRow("png", ".png")]
    [DataRow("*.png", ".png")]
    [DataRow(".import-manifest.json", ".import-manifest.json")]
    public void NormalizeExtension_AcceptsTheCommonSpellings(string extension, string expected)
        => _ = DialogService.NormalizeExtension(extension).Should().Be(expected);

    [TestMethod]
    [DataRow("")]
    [DataRow(" ")]
    [DataRow("*")]
    [DataRow(".*")]
    [DataRow("p ng")]
    [DataRow("png.")]
    [DataRow("a/b")]
    public void NormalizeExtension_RejectsWhatIsNotAnExtension(string extension)
        => _ = FluentActions.Invoking(() => DialogService.NormalizeExtension(extension)).Should().Throw<ArgumentException>();

    [TestMethod]
    public async Task Pickers_MissingOwnerAndCancellationAreExplicitAsync()
    {
        var service = new DialogService(Mock.Of<IWindowManagerService>());
        var folder = new FolderPickerSpec("Folder", "Test.Folder");
        var file = new FilePickerSpec("File", "Test.File", new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal) { ["Text"] = [".txt"] });
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
        var file = new FilePickerSpec("File", "Test.File", new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal) { ["Text"] = [".txt"] });
        Func<Task> pickFolder = () => service.PickFolderAsync(folder, new WindowId(42), this.TestContext.CancellationToken);
        Func<Task> pickFile = () => service.PickFileAsync(file, new WindowId(42), this.TestContext.CancellationToken);
        _ = await pickFolder.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        _ = await pickFile.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        windows.VerifyGet(value => value.ActiveWindow, Times.Never());
    }
}
