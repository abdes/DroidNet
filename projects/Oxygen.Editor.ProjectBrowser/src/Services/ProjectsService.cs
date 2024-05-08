// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Services;

using System.Diagnostics;
using System.Runtime.CompilerServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Storage;

/// <summary>Provides method to access and manipulate projects.</summary>
public class ProjectsService : IProjectsService
{
    private readonly IPathFinder finder;
    private readonly IProjectSource projectsSource;

    public ProjectsService(IProjectSource source, IPathFinder finder)
    {
        this.projectsSource = source;
        this.finder = finder;
    }

    public async Task<bool> NewProjectFromTemplate(
        ITemplateInfo templateInfo,
        string projectName,
        string atLocationPath)
    {
        if (!this.CanCreateProject(projectName, atLocationPath))
        {
            Debug.WriteLine($"Cannot create a new project with name `{projectName}` at: {atLocationPath}");
            return false;
        }

        if (string.IsNullOrEmpty(templateInfo.Location))
        {
            Debug.WriteLine($"Invalid template location: {templateInfo.Location}");
            return false;
        }

        var templateDirInfo = new DirectoryInfo(templateInfo.Location!);

        IFolder? projectFolder = null;
        try
        {
            // Create project folder
            projectFolder = await this.projectsSource.CreateNewProjectFolder(projectName, atLocationPath);
            if (projectFolder == null)
            {
                return false;
            }

            // Copy all content from template to the new project folder
            _ = Parallel.ForEach(
                templateDirInfo.GetDirectories("*", SearchOption.AllDirectories),
                srcInfo => Directory.CreateDirectory(
                    $"{projectFolder.Location}{srcInfo.FullName[templateDirInfo.FullName.Length..]}"));

            _ = Parallel.ForEach(
                templateDirInfo.GetFiles("*", SearchOption.AllDirectories),
                srcInfo =>
                {
                    if (srcInfo.Name != "Template.json")
                    {
                        File.Copy(
                            srcInfo.FullName,
                            $"{projectFolder.Location}{srcInfo.FullName[templateDirInfo.FullName.Length..]}",
                            true);
                    }
                });

            // Load the project info, update it, and save it
            var projectInfo = await this.projectsSource.LoadProjectInfoAsync(projectFolder.Location);
            if (projectInfo != null)
            {
                projectInfo.Name = projectName;
                if (await this.projectsSource.SaveProjectInfoAsync(projectInfo))
                {
                    // Update the recently used project entry for the project being saved
                    await TryUpdateRecentUsageAsync(projectInfo.Location!);

                    // Update the last save location
                    await TryUpdateLastSaveLocation(new DirectoryInfo(projectInfo.Location!).Parent!.FullName);

                    return true;
                }
            }
        }
        catch (Exception copyError)
        {
            Debug.WriteLine($"Project creation failed: {copyError.Message}");
        }

        RemoveFailedProject(projectFolder);
        return false;

        static void RemoveFailedProject(IFolder? storageLocation)
        {
            if (storageLocation == null)
            {
                return;
            }

            try
            {
                Directory.Delete(storageLocation.Location, true);
            }
            catch (Exception deleteError)
            {
                Debug.WriteLine(
                    $"An error occurred while trying to delete a failed project folder at `{storageLocation.Location}`: {deleteError.Message}");
            }
        }
    }

    public bool CanCreateProject(string projectName, string atLocationPath)
        => this.projectsSource.CanCreateProject(projectName, atLocationPath);

    public async Task<bool> LoadProjectAsync(string location)
    {
        await TryUpdateRecentUsageAsync(location);

        // TODO(abdes) load the project
        return true;
    }

    public List<QuickSaveLocation> GetQuickSaveLocations()
    {
        var locations = new List<QuickSaveLocation>();

        using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();

        var lastSaveLocation = state.ProjectBrowserState.LastSaveLocation;
        if (lastSaveLocation != string.Empty)
        {
            locations.Add(new QuickSaveLocation("Recently Used", lastSaveLocation));
        }

        locations.Add(new QuickSaveLocation("Personal Projects", Path.GetFullPath(this.finder.PersonalProjects)));
        locations.Add(new QuickSaveLocation("Local Projects", Path.GetFullPath(this.finder.LocalProjects)));

        return locations;
    }

    public async IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();
        foreach (var item in state.RecentlyUsedProjects)
        {
            var projectInfo = await this.projectsSource.LoadProjectInfoAsync(item.Location!);
            if (projectInfo != null)
            {
                projectInfo.LastUsedOn = item.LastUsedOn;
                yield return projectInfo;
            }
            else
            {
                try
                {
                    _ = state.RecentlyUsedProjects!.Remove(item);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Failed to remove entry from the most recently used projects: {ex.Message}");
                }
            }
        }

        _ = await state.SaveChangesAsync(cancellationToken);
    }

    private static async Task TryUpdateRecentUsageAsync(string location)
    {
        try
        {
            await UpdateRecentUsageAsync(location);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
        }
    }

    private static async Task UpdateRecentUsageAsync(string location)
    {
        await using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();

        var recentProjectEntry = state.RecentlyUsedProjects.ToList()
            .SingleOrDefault(p => p.Location == location, new RecentlyUsedProject(0, location));
        if (recentProjectEntry.Id != 0)
        {
            recentProjectEntry.LastUsedOn = DateTime.Now;
            _ = state.RecentlyUsedProjects!.Update(recentProjectEntry);
        }
        else
        {
            state.ProjectBrowserState.RecentProjects.Add(recentProjectEntry);
        }

        _ = await state.SaveChangesAsync();
    }

    private static async Task TryUpdateLastSaveLocation(string location)
    {
        try
        {
            await UpdateLastSaveLocation(location);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
        }
    }

    private static async Task UpdateLastSaveLocation(string location)
    {
        await using var state = Ioc.Default.CreateScope()
            .ServiceProvider.GetRequiredService<PersistentState>();

        state.ProjectBrowserState.LastSaveLocation = location;

        _ = await state.SaveChangesAsync();
    }
}
