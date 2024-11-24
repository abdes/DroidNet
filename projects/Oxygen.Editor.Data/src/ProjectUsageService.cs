// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data;

/// <summary>
/// Provides services for managing project usage data, including retrieving, updating, and validating project usage records.
/// </summary>
/// <param name="context">The database context to be used by the service.</param>
/// <remarks>
/// This service is designed to work with the <see cref="PersistentState"/> context to manage project usage data.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var contextOptions = new DbContextOptionsBuilder<PersistentState>()
///     .UseInMemoryDatabase(databaseName: "TestDatabase")
///     .Options;
/// var context = new PersistentState(contextOptions);
/// var service = new ProjectUsageService(context);
///
/// // Add or update a project usage record
/// await service.UpdateProjectUsageAsync("Project1", "Location1");
///
/// // Retrieve the most recently used projects
/// var recentProjects = await service.GetMostRecentlyUsedProjectsAsync();
/// foreach (var project in recentProjects)
/// {
///     Console.WriteLine($"Project: {project.Name}, Last Used: {project.LastUsedOn}");
/// }
///
/// // Update the content browser state
/// await service.UpdateContentBrowserStateAsync("Project1", "Location1", "NewState");
///
/// // Rename or move a project
/// await service.UpdateProjectNameAndLocationAsync("Project1", "Location1", "NewProject1", "NewLocation1");
/// ]]></code>
/// </example>
public class ProjectUsageService(PersistentState context)
{
    /// <summary>
    /// Gets the 10 most recently used projects, sorted in descending order by the last used date.
    /// </summary>
    /// <returns>A list of the 10 most recently used projects.</returns>
    public async Task<IList<ProjectUsage>> GetMostRecentlyUsedProjectsAsync() => await context.ProjectUsageRecords
            .OrderByDescending(p => p.LastUsedOn)
            .Take(10)
            .ToListAsync().ConfigureAwait(false);

    /// <summary>
    /// Gets the project usage data for a project given its name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <returns>The project usage data if found; otherwise, <see langword="null"/>.</returns>
    public async Task<ProjectUsage?> GetProjectUsageAsync(string name, string location) => await context.ProjectUsageRecords
            .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

    /// <summary>
    /// Updates a project usage record given the project name and location. If the project record already exists, increments its <see cref="ProjectUsage.TimesOpened"/> counter. If the record does not exist, creates a new record with <see cref="ProjectUsage.TimesOpened"/> set to 1.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    /// <exception cref="ValidationException">Thrown when the project name or location is invalid.</exception>
    public async Task UpdateProjectUsageAsync(string name, string location)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(false);
        if (projectUsage != null)
        {
            projectUsage.TimesOpened++;
            projectUsage.LastUsedOn = DateTime.Now;
        }
        else
        {
            ValidateProjectNameAndLocation(name, location);

            projectUsage = new ProjectUsage
            {
                Name = name,
                Location = location,
                TimesOpened = 1,
                LastUsedOn = DateTime.Now,
            };
            _ = context.ProjectUsageRecords.Add(projectUsage);
        }

        _ = await context.SaveChangesAsync().ConfigureAwait(false);
    }

    /// <summary>
    /// Updates the content browser state of a project usage record given the project name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <param name="contentBrowserState">The new content browser state.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    /// <remarks>
    /// This method expects the project usage record to already exist. If the record does not exist, a debug assertion will fire.
    /// </remarks>
    public async Task UpdateContentBrowserStateAsync(string name, string location, string contentBrowserState)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(false);

        if (projectUsage != null)
        {
            projectUsage.ContentBrowserState = contentBrowserState;
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
        }

        Debug.Assert(projectUsage != null, "Project usage record must exist before updating content browser state (did you call UpdateProjectUsageAsync() before).");
    }

    /// <summary>
    /// Updates the last opened scene of a project usage record given the project name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <param name="lastOpenedScene">The new last opened scene.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    /// <remarks>
    /// This method expects the project usage record to already exist. If the record does not exist, a debug assertion will fire.
    /// </remarks>
    public async Task UpdateLastOpenedSceneAsync(string name, string location, string lastOpenedScene)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(false);

        if (projectUsage != null)
        {
            projectUsage.LastOpenedScene = lastOpenedScene;
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
        }

        Debug.Assert(projectUsage != null, "Project usage record must exist before updating last opened scene (did you call UpdateProjectUsageAsync() before).");
    }

    /// <summary>
    /// Updates a project usage record's name and location when the project has been renamed or moved.
    /// </summary>
    /// <param name="oldName">The old name of the project.</param>
    /// <param name="oldLocation">The old location of the project.</param>
    /// <param name="newName">The new name of the project.</param>
    /// <param name="newLocation">The new location of the project (optional).</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    /// <exception cref="ValidationException">Thrown when the new project name or location is invalid.</exception>
    public async Task UpdateProjectNameAndLocationAsync(string oldName, string oldLocation, string newName, string? newLocation = null)
    {
        ValidateProjectNameAndLocation(newName, newLocation ?? oldLocation);

        var projectUsage = await this.GetProjectUsageAsync(oldName, oldLocation).ConfigureAwait(false);
        if (projectUsage != null)
        {
            projectUsage.Name = newName;
            if (!string.IsNullOrEmpty(newLocation))
            {
                projectUsage.Location = newLocation;
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Validates the project name and location to ensure they are not empty or whitespace.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <exception cref="ValidationException">Thrown when the project name or location is invalid.</exception>
    private static void ValidateProjectNameAndLocation(string name, string location)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length < 1)
        {
            throw new ValidationException("The project name cannot be empty.");
        }

        if (string.IsNullOrWhiteSpace(location) || location.Length < 1)
        {
            throw new ValidationException("The project location cannot be empty.");
        }
    }
}
