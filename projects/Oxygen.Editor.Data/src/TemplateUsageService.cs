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
/// var service = new TemplateUsageService(context, cache);
///
/// // Add or update a template usage record
/// await service.UpdateTemplateUsageAsync("Location1");
///
/// // Retrieve the most recently used templates
/// var recentTemplates = await service.GetMostRecentlyUsedTemplatesAsync();
/// foreach (var template in recentTemplates)
/// {
///     Console.WriteLine($"Template Location: {template.Location}, Last Used: {template.LastUsedOn}");
/// }
///
/// // Get a specific template usage record
/// var templateUsage = await service.GetTemplateUsageAsync("Location1");
/// if (templateUsage != null)
/// {
///     Console.WriteLine($"Template Location: {templateUsage.Location}, Times Used: {templateUsage.TimesUsed}");
/// }
/// else
/// {
///     Console.WriteLine("Template not found.");
/// }
/// ]]></code>
/// </example>
public class TemplateUsageService(PersistentState context, IMemoryCache cache) : ITemplateUsageService
{
    /// <summary>
    /// The prefix to be used for cache keys related to template usage data.
    /// </summary>
    internal const string CacheKeyPrefix = "TemplateUsage_";

    /// <inheritdoc />
    public async Task<IList<TemplateUsage>> GetMostRecentlyUsedTemplatesAsync(int sizeLimit = 10) => await context.TemplatesUsageRecords
            .OrderByDescending(t => t.LastUsedOn)
            .Take(sizeLimit > 0 ? sizeLimit : 10)
            .ToListAsync().ConfigureAwait(false);

    /// <inheritdoc />
    public async Task<TemplateUsage?> GetTemplateUsageAsync(string location)
    {
        if (cache.TryGetValue(CacheKeyPrefix + location, out TemplateUsage? templateUsage))
        {
            return templateUsage;
        }

        templateUsage = await context.TemplatesUsageRecords
            .FirstOrDefaultAsync(t => t.Location == location).ConfigureAwait(false);

        if (templateUsage != null)
        {
            _ = cache.Set(CacheKeyPrefix + location, templateUsage);
        }

        return templateUsage;
    }

    /// <inheritdoc />
    public async Task UpdateTemplateUsageAsync(string location)
    {
        var templateUsage = await this.GetTemplateUsageAsync(location).ConfigureAwait(false);
        if (templateUsage != null)
        {
            templateUsage.TimesUsed++;
            templateUsage.LastUsedOn = DateTime.Now;
        }
        else
        {
            ValidateTemplateLocation(location);

            templateUsage = new TemplateUsage
            {
                Location = location,
                TimesUsed = 1,
                LastUsedOn = DateTime.Now,
            };
            _ = context.TemplatesUsageRecords.Add(templateUsage);
        }

        _ = await context.SaveChangesAsync().ConfigureAwait(false);
        _ = cache.Set(CacheKeyPrefix + location, templateUsage);
    }

    /// <inheritdoc />
    public async Task<bool> HasRecentlyUsedTemplatesAsync() => await context.TemplatesUsageRecords.AnyAsync().ConfigureAwait(false);

    /// <inheritdoc />
    public async Task DeleteTemplateUsageAsync(string location)
    {
        var templateUsage = await this.GetTemplateUsageAsync(location).ConfigureAwait(false);
        if (templateUsage != null)
        {
            _ = context.TemplatesUsageRecords.Remove(templateUsage);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);
            cache.Remove(CacheKeyPrefix + location);
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
}
