// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Service for managing window decoration settings with code-defined defaults and persisted category overrides.
/// </summary>
/// <remarks>
/// <para>
/// This service extends <see cref="ISettingsService{TSettings}"/> with domain-specific methods for resolving
/// effective window decorations by combining code-defined defaults with persisted overrides.
/// </para>
/// </remarks>
public interface IWindowDecorationSettingsService : ISettingsService<WindowDecorationSettings>
{
    /// <summary>
    /// Resolves the effective decoration options for a given window category.
    /// </summary>
    /// <param name="category">The window category (e.g., "Main", "Tool", "Document").</param>
    /// <returns>
    /// The effective <see cref="WindowDecorationOptions"/> for the category, combining code-defined
    /// defaults with any persisted overrides.
    /// </returns>
    /// <remarks>
    /// <para>
    /// Resolution order:
    /// </para>
    /// <list type="number">
    /// <item><description>Check for a persisted category override</description></item>
    /// <item><description>Fall back to code-defined default for the category</description></item>
    /// <item><description>Fall back to "Unknown" category default if category not recognized</description></item>
    /// </list>
    /// </remarks>
    public WindowDecorationOptions GetEffectiveDecoration(WindowCategory category);

    /// <summary>
    /// Sets a persisted override for a window category, replacing any existing override.
    /// </summary>
    /// <param name="category">The window category to override.</param>
    /// <param name="options">The decoration options to persist as an override.</param>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="options"/> is null.</exception>
    /// <exception cref="ValidationException">Thrown if <paramref name="options"/> fails validation.</exception>
    public void SetCategoryOverride(WindowCategory category, WindowDecorationOptions options);

    /// <summary>
    /// Removes a persisted category override, reverting to the code-defined default.
    /// </summary>
    /// <param name="category">The window category to revert to default.</param>
    /// <returns>
    /// <see langword="true"/> if an override was removed; <see langword="false"/> if no override existed.
    /// </returns>
    public bool RemoveCategoryOverride(WindowCategory category);

    /// <summary>
    /// Saves all current decoration settings to persistent storage asynchronously.
    /// </summary>
    /// <param name="cancellationToken">Cancellation token for the save operation.</param>
    /// <returns>
    /// A task that represents the asynchronous save operation. The task result is <see langword="true"/>
    /// if the settings were successfully persisted; otherwise, <see langword="false"/>.
    /// </returns>
    public ValueTask<bool> SaveAsync(CancellationToken cancellationToken = default);
}
