// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using Microsoft.EntityFrameworkCore;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data;

/// <summary>
/// Provides services for managing template usage data, including retrieving, updating, and validating template usage records.
/// </summary>
/// <remarks>
/// <param name="context">The database context to be used by the service.</param>
/// This service is designed to work with the <see cref="PersistentState"/> context to manage template usage data.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var contextOptions = new DbContextOptionsBuilder<PersistentState>()
///     .UseInMemoryDatabase(databaseName: "TestDatabase")
///     .Options;
/// var context = new PersistentState(contextOptions);
/// var service = new TemplateUsageService(context);
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
public class TemplateUsageService(PersistentState context)
{
    /// <summary>
    /// Gets the 10 most recently used templates, sorted in descending order by the last used date.
    /// </summary>
    /// <returns>A list of the 10 most recently used templates.</returns>
    public async Task<IList<TemplateUsage>> GetMostRecentlyUsedTemplatesAsync() => await context.TemplatesUsageRecords
            .OrderByDescending(t => t.LastUsedOn)
            .Take(10)
            .ToListAsync().ConfigureAwait(false);

    /// <summary>
    /// Gets the template usage data for a template given its location.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <returns>The template usage data if found; otherwise, <see langword="null"/>.</returns>
    public async Task<TemplateUsage?> GetTemplateUsageAsync(string location) => await context.TemplatesUsageRecords
            .FirstOrDefaultAsync(t => t.Location == location).ConfigureAwait(false);

    /// <summary>
    /// Updates a template usage record given the template location. If the template record already exists, increments its <see cref="TemplateUsage.TimesUsed"/> counter. If the record does not exist, creates a new record with <see cref="TemplateUsage.TimesUsed"/> set to 1.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    /// <exception cref="ValidationException">Thrown when the template location is invalid.</exception>
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
