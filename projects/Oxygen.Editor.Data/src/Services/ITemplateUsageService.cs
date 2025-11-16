// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Specifies a service for managing and retrieving template usage data.
/// </summary>
/// <remarks>
/// Implementations of this service are intended to be used as a `Singleton` inside the application.
/// When such implementation is caching the data, it is important to ensure that the cache remains
/// coherent at all times or gets invalidated automatically without requiring an API call to do so.
/// </remarks>
public interface ITemplateUsageService
{
    /// <summary>
    /// Determines whether there are any templates that have been used recently.
    /// </summary>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of
    /// <see langword="true"/> if there are recently used templates; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> HasRecentlyUsedTemplatesAsync();

    /// <summary>
    /// Retrieves a list of the most recently used templates.
    /// </summary>
    /// <param name="sizeLimit">The maximum number of items to retrieve; default is <c>10</c>.</param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of a
    /// list of <see cref="TemplateUsage"/> objects. When no recently used templates are found, the
    /// list will be empty.
    /// </returns>
    public Task<IList<TemplateUsage>> GetMostRecentlyUsedTemplatesAsync(int sizeLimit = 10);

    /// <summary>
    /// Retrieves the usage data for a specific template identified by its location.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of the
    /// <see cref="TemplateUsage"/> object if found; otherwise, <see langword="null"/>.
    /// </returns>
    public Task<TemplateUsage?> GetTemplateUsageAsync(string location);

    /// <summary>
    /// Updates the usage data for a specific template identified by its location, including the last used date and incrementing the usage count.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task UpdateTemplateUsageAsync(string location);

    /// <summary>
    /// Deletes the usage data for a specific template identified by its <paramref name="location"/>.
    /// </summary>
    /// <param name="location">The location of the template.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task DeleteTemplateUsageAsync(string location);
}
