// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Provides services for managing project usage data, including retrieving, updating, and validating project usage records.
/// </summary>
/// <remarks>
/// This service is designed to work with the <see cref="PersistentState"/> context to manage project usage data.
/// It uses an <see cref="IMemoryCache"/> to cache project usage data for improved performance.
/// </remarks>
/// <param name="context">The database context to be used by the service.</param>
/// <param name="cache">The memory cache to be used by the service.</param>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var contextOptions = new DbContextOptionsBuilder<PersistentState>()
///     .UseInMemoryDatabase(databaseName: "TestDatabase")
///     .Options;
/// var context = new PersistentState(contextOptions);
/// var cache = new MemoryCache(new MemoryCacheOptions());
/// var service = new ProjectUsageService(context, cache);
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
public class ProjectUsageService(PersistentState context, IMemoryCache cache) : IProjectUsageService
{
    /// <summary>
    /// The prefix to be used for cache keys related to project usage data.
    /// </summary>
    internal const string CacheKeyPrefix = "ProjectUsage_";

    /// <inheritdoc />
    public async Task<bool> HasRecentlyUsedProjectsAsync() => await context.ProjectUsageRecords.AnyAsync().ConfigureAwait(true);

    /// <inheritdoc />
    public async Task<IList<ProjectUsage>> GetMostRecentlyUsedProjectsAsync(uint sizeLimit = 10) => await context.ProjectUsageRecords
            .OrderByDescending(p => p.LastUsedOn)
            .Take((int)sizeLimit > 0 ? (int)sizeLimit : 10)
            .ToListAsync().ConfigureAwait(true);

    /// <inheritdoc />
    public async Task<ProjectUsage?> GetProjectUsageAsync(string name, string location)
    {
        var cacheKey = CacheKeyPrefix + name + "_" + location;
        if (cache.TryGetValue(cacheKey, out ProjectUsage? projectUsage))
        {
            return projectUsage;
        }

        projectUsage = await context.ProjectUsageRecords
            .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(true);

        if (projectUsage != null)
        {
            _ = cache.Set(cacheKey, projectUsage);
        }

        return projectUsage;
    }

    /// <inheritdoc />
    public async Task UpdateProjectUsageAsync(string name, string location)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(true);
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

        _ = await context.SaveChangesAsync().ConfigureAwait(true);
        _ = cache.Set(CacheKeyPrefix + name + "_" + location, projectUsage);
    }

    /// <inheritdoc />
    public async Task UpdateContentBrowserStateAsync(string name, string location, string contentBrowserState)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(true);

        if (projectUsage != null)
        {
            projectUsage.ContentBrowserState = contentBrowserState;
            _ = await context.SaveChangesAsync().ConfigureAwait(true);
            _ = cache.Set(CacheKeyPrefix + name + "_" + location, projectUsage);
        }

        Debug.Assert(projectUsage != null, "Project usage record must exist before updating content browser state (did you call UpdateProjectUsageAsync() before).");
    }

    /// <inheritdoc />
    public async Task UpdateLastOpenedSceneAsync(string name, string location, string lastOpenedScene)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(true);

        if (projectUsage != null)
        {
            projectUsage.LastOpenedScene = lastOpenedScene;
            _ = await context.SaveChangesAsync().ConfigureAwait(true);
            _ = cache.Set(CacheKeyPrefix + name + "_" + location, projectUsage);
        }

        Debug.Assert(projectUsage != null, "Project usage record must exist before updating last opened scene (did you call UpdateProjectUsageAsync() before).");
    }

    /// <inheritdoc />
    public async Task UpdateProjectNameAndLocationAsync(string oldName, string oldLocation, string newName, string? newLocation = null)
    {
        ValidateProjectNameAndLocation(newName, newLocation ?? oldLocation);

        var projectUsage = await this.GetProjectUsageAsync(oldName, oldLocation).ConfigureAwait(true);
        if (projectUsage != null)
        {
            projectUsage.Name = newName;
            if (!string.IsNullOrEmpty(newLocation))
            {
                projectUsage.Location = newLocation;
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(true);
            _ = cache.Set(CacheKeyPrefix + newName + "_" + (newLocation ?? oldLocation), projectUsage);
            cache.Remove(CacheKeyPrefix + oldName + "_" + oldLocation);
        }
    }

    /// <inheritdoc />
    public async Task DeleteProjectUsageAsync(string name, string location)
    {
        var projectUsage = await this.GetProjectUsageAsync(name, location).ConfigureAwait(true);
        if (projectUsage != null)
        {
            // Ensure we remove the tracked entity from the current context
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(true);
            if (tracked != null)
            {
                _ = context.ProjectUsageRecords.Remove(tracked);
                _ = await context.SaveChangesAsync().ConfigureAwait(true);
            }

            cache.Remove(CacheKeyPrefix + name + "_" + location);
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
