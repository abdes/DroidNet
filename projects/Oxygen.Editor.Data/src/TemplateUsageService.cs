// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data;

/// <summary>
/// Provides services for managing template usage data, including retrieving, updating, and validating template usage records.
/// </summary>
/// <remarks>
/// This service is designed to work with the <see cref="PersistentState"/> context to manage template usage data.
/// It uses an <see cref="IMemoryCache"/> to cache template usage data for improved performance.
/// </remarks>
public class TemplateUsageService : ITemplateUsageService
{
    /// <summary>
    /// The prefix to be used for cache keys related to template usage data.
    /// </summary>
    internal const string CacheKeyPrefix = "TemplateUsage_";

    private readonly Func<PersistentState> contextFactory;
    private readonly IMemoryCache cache;

    /// <summary>
    /// Initializes a new instance of the <see cref="TemplateUsageService"/> class.
    /// </summary>
    /// <param name="contextFactory">A factory function to create a <see cref="PersistentState"/> context.</param>
    /// <param name="cache">The memory cache used for caching template usage data.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "does not make sense when we are checking for null arguments")]
    public TemplateUsageService(Func<PersistentState> contextFactory, IMemoryCache cache)
    {
        this.contextFactory = contextFactory ?? throw new ArgumentNullException(nameof(contextFactory));
        this.cache = cache ?? throw new ArgumentNullException(nameof(cache));
    }

    /// <inheritdoc />
    public async Task<IList<TemplateUsage>> GetMostRecentlyUsedTemplatesAsync(int sizeLimit = 10)
    {
        var context = this.contextFactory();
        try
        {
            var records = await context.TemplatesUsageRecords
                .AsNoTracking()
                .OrderByDescending(t => t.LastUsedOn)
                .Take(sizeLimit > 0 ? sizeLimit : 10)
                .ToListAsync().ConfigureAwait(false);

            // Return detached clones
            return records.Select(Clone).ToList();
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<TemplateUsage?> GetTemplateUsageAsync(string location)
    {
        if (this.cache.TryGetValue(CacheKeyPrefix + location, out TemplateUsage? cached))
        {
            return cached is null ? null : Clone(cached);
        }

        var context = this.contextFactory();
        try
        {
            var templateUsage = await context.TemplatesUsageRecords
                .AsNoTracking()
                .FirstOrDefaultAsync(t => t.Location == location).ConfigureAwait(false);

            if (templateUsage != null)
            {
                var detached = Clone(templateUsage);
                _ = this.cache.Set(CacheKeyPrefix + location, detached);
                return Clone(detached);
            }

            return null;
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task UpdateTemplateUsageAsync(string location)
    {
        ValidateTemplateLocation(location);

        var context = this.contextFactory();
        try
        {
            // Use tracked entity for updates
            var tracked = await context.TemplatesUsageRecords
                .FirstOrDefaultAsync(t => t.Location == location).ConfigureAwait(false);

            if (tracked != null)
            {
                tracked.TimesUsed++;
                tracked.LastUsedOn = DateTime.UtcNow;
            }
            else
            {
                tracked = new TemplateUsage
                {
                    Location = location,
                    TimesUsed = 1,
                    LastUsedOn = DateTime.UtcNow,
                };
                _ = context.TemplatesUsageRecords.Add(tracked);
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            var cached = Clone(tracked);
            _ = this.cache.Set(CacheKeyPrefix + location, cached);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task<bool> HasRecentlyUsedTemplatesAsync()
    {
        var context = this.contextFactory();
        try
        {
            return await context.TemplatesUsageRecords.AnyAsync().ConfigureAwait(false);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async Task DeleteTemplateUsageAsync(string location)
    {
        var context = this.contextFactory();
        try
        {
            // Retrieve a tracked entity for deletion
            var tracked = await context.TemplatesUsageRecords.FirstOrDefaultAsync(t => t.Location == location).ConfigureAwait(false);
            if (tracked != null)
            {
                _ = context.TemplatesUsageRecords.Remove(tracked);
                _ = await context.SaveChangesAsync().ConfigureAwait(false);
                this.cache.Remove(CacheKeyPrefix + location);
            }
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Validates the template location to ensure it is not empty or whitespace.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <exception cref="ValidationException">Thrown when the template location is invalid.</exception>
    private static void ValidateTemplateLocation(string location)
    {
        if (string.IsNullOrWhiteSpace(location) || location.Length < 1)
        {
            throw new ValidationException("The template location cannot be empty.");
        }
    }

    private static TemplateUsage Clone(TemplateUsage src)
    {
        return new TemplateUsage
        {
            Id = src.Id,
            Location = src.Location,
            TimesUsed = src.TimesUsed,
            LastUsedOn = src.LastUsedOn,
        };
    }
}
