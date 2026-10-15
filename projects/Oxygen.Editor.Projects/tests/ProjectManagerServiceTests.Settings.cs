// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.World;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies the project settings saved in the manifest.</summary>
public partial class ProjectManagerServiceTests
{
    /// <summary>A manifest written before settings existed reads with the default settings.</summary>
    [TestMethod]
    public void ManifestWithoutSettingsReadsTheDefaults()
    {
        var json = ProjectInfo.ToJson(new ProjectInfo("Settings", Category.Games)).Replace("\"Settings\"", "\"Unknown\"", StringComparison.Ordinal);

        var info = ProjectInfo.FromJson(json);

        _ = info.Settings.Should().Be(new ProjectSettings());
        _ = info.Settings.CookOnChange.Should().BeTrue();
    }

    /// <summary>Saving the settings changes only the settings and keeps an outside edit of the rest of the manifest.</summary>
    /// <returns>The asynchronous settings-save regression.</returns>
    [TestMethod]
    public async Task SettingsSaveKeepsTheRestOfTheManifest()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var original = new ProjectInfo("Settings", Category.Games, workspace.Root) { AuthoringMounts = [new("Content", "Content")] };
        _ = (await service.SaveProjectInfoAsync(original).ConfigureAwait(false)).Should().BeTrue();
        var path = Path.Combine(workspace.Root, Constants.ProjectFileName);
        var external = new ProjectInfo(original.Id, "Externally edited", original.Category, original.Location) { AuthoringMounts = [.. original.AuthoringMounts] };
        await File.WriteAllTextAsync(path, ProjectInfo.ToJson(external), this.CancellationToken).ConfigureAwait(false);

        await service.SaveProjectSettingsAsync(workspace.Root, new ProjectSettings { CookOnChange = false }, this.CancellationToken).ConfigureAwait(false);

        var saved = ProjectInfo.FromJson(await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false));
        _ = saved.Settings.CookOnChange.Should().BeFalse();
        _ = saved.Name.Should().Be("Externally edited");
        _ = saved.AuthoringMounts.Should().Equal(original.AuthoringMounts);
    }
}
