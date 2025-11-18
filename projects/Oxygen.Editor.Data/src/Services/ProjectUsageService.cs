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
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var contextOptions = new DbContextOptionsBuilder<PersistentState>()
///     .UseInMemoryDatabase(databaseName: "TestDatabase")
///     .Options;
/// var cache = new MemoryCache(new MemoryCacheOptions());
/// var service = new ProjectUsageService(() => new PersistentState(contextOptions), cache);
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
public class ProjectUsageService : IProjectUsageService
{
    /// <summary>
    /// The prefix to be used for cache keys related to project usage data.
    /// </summary>
    internal const string CacheKeyPrefix = "ProjectUsage_";

    private readonly Func<PersistentState> contextFactory;
    private readonly IMemoryCache cache;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectUsageService"/> class.
    /// </summary>
    /// <param name="contextFactory">Factory used to create new <see cref="PersistentState"/> instances.</param>
    /// <param name="cache">Shared cache used to store project usage records.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "null checking is required")]
    public ProjectUsageService(Func<PersistentState> contextFactory, IMemoryCache cache)
    {
        this.contextFactory = contextFactory ?? throw new ArgumentNullException(nameof(contextFactory));
        this.cache = cache ?? throw new ArgumentNullException(nameof(cache));
    }

    /// <inheritdoc />
    public async Task<bool> HasRecentlyUsedProjectsAsync()
    {
        var context = this.contextFactory();
        try
        {
            return await context.ProjectUsageRecords.AnyAsync().ConfigureAwait(false);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<IList<ProjectUsage>> GetMostRecentlyUsedProjectsAsync(uint sizeLimit = 10)
    {
        var context = this.contextFactory();
        try
        {
            var records = await context.ProjectUsageRecords
                .AsNoTracking()
                .OrderByDescending(p => p.LastUsedOn)
                .Take((int)sizeLimit > 0 ? (int)sizeLimit : 10)
                .ToListAsync().ConfigureAwait(false);

            return records.ConvertAll(Clone);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<ProjectUsage?> GetProjectUsageAsync(string name, string location)
    {
        var cacheKey = CreateCacheKey(name, location);
        if (this.cache.TryGetValue(cacheKey, out ProjectUsage? cached))
        {
            return cached == null ? null : Clone(cached);
        }

        var context = this.contextFactory();
        try
        {
            var projectUsage = await context.ProjectUsageRecords
                .AsNoTracking()
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

            if (projectUsage == null)
            {
                return null;
            }

            var detached = Clone(projectUsage);
            _ = this.cache.Set(cacheKey, detached);
            return Clone(detached);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task UpdateProjectUsageAsync(string name, string location)
    {
        ValidateProjectNameAndLocation(name, location);
        var cacheKey = CreateCacheKey(name, location);

        var context = this.contextFactory();
        try
        {
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

            if (tracked != null)
            {
                tracked.TimesOpened++;
                tracked.LastUsedOn = DateTime.Now;
            }
            else
            {
                var cached = this.cache.Get<ProjectUsage>(cacheKey);
                tracked = new ProjectUsage
                {
                    Name = name,
                    Location = location,
                    TimesOpened = (cached?.TimesOpened ?? 0) + 1,
                    LastUsedOn = DateTime.Now,
                    LastOpenedScene = cached?.LastOpenedScene ?? string.Empty,
                    ContentBrowserState = cached?.ContentBrowserState ?? string.Empty,
                };
                _ = context.ProjectUsageRecords.Add(tracked);
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(false);
            _ = this.cache.Set(cacheKey, Clone(tracked));
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task UpdateContentBrowserStateAsync(string name, string location, string contentBrowserState)
    {
        var context = this.contextFactory();
        try
        {
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

            if (tracked == null)
            {
                Debug.Fail("Project usage record must exist before updating content browser state (did you call UpdateProjectUsageAsync() before).");
                return;
            }

            tracked.ContentBrowserState = contentBrowserState;
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
            _ = this.cache.Set(CreateCacheKey(name, location), Clone(tracked));
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task UpdateLastOpenedSceneAsync(string name, string location, string lastOpenedScene)
    {
        var context = this.contextFactory();
        try
        {
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

            if (tracked == null)
            {
                Debug.Fail("Project usage record must exist before updating last opened scene (did you call UpdateProjectUsageAsync() before).");
                return;
            }

            tracked.LastOpenedScene = lastOpenedScene;
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
            _ = this.cache.Set(CreateCacheKey(name, location), Clone(tracked));
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task UpdateProjectNameAndLocationAsync(string oldName, string oldLocation, string newName, string? newLocation = null)
    {
        ValidateProjectNameAndLocation(newName, newLocation ?? oldLocation);

        var context = this.contextFactory();
        try
        {
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == oldName && p.Location == oldLocation).ConfigureAwait(false);

            if (tracked == null)
            {
                return;
            }

            tracked.Name = newName;
            if (!string.IsNullOrEmpty(newLocation))
            {
                tracked.Location = newLocation;
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            var updatedKey = CreateCacheKey(tracked.Name, tracked.Location);
            _ = this.cache.Set(updatedKey, Clone(tracked));
            this.cache.Remove(CreateCacheKey(oldName, oldLocation));
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task DeleteProjectUsageAsync(string name, string location)
    {
        var context = this.contextFactory();
        try
        {
            var tracked = await context.ProjectUsageRecords
                .FirstOrDefaultAsync(p => p.Name == name && p.Location == location).ConfigureAwait(false);

            if (tracked == null)
            {
                return;
            }

            _ = context.ProjectUsageRecords.Remove(tracked);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
            this.cache.Remove(CreateCacheKey(name, location));
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
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

    private static string CreateCacheKey(string name, string location) => CacheKeyPrefix + name + "_" + location;

    private static ProjectUsage Clone(ProjectUsage source)
        => new()
        {
            Id = source.Id,
            Name = source.Name,
            Location = source.Location,
            TimesOpened = source.TimesOpened,
            LastUsedOn = source.LastUsedOn,
            LastOpenedScene = source.LastOpenedScene,
            ContentBrowserState = source.ContentBrowserState,
        };
}
