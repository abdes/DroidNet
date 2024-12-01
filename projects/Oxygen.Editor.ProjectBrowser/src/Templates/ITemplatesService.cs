// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Defines a service for managing and accessing project templates in the Oxygen Editor.
/// </summary>
/// <remarks>
/// <para>
/// The service supports both built-in templates shipped with the editor and custom templates
/// stored in user-defined locations.
/// </para>
/// </remarks>
public interface ITemplatesService
{
    /// <summary>
    /// Gets an asynchronous enumerable of all available local templates.
    /// </summary>
    /// <returns>
    /// An asynchronous enumerable of <see cref="ITemplateInfo"/> objects representing available templates.
    /// </returns>
    /// <remarks>
    /// Templates that fail to load are skipped and errors are logged without interrupting enumeration.
    /// </remarks>
    public IAsyncEnumerable<ITemplateInfo> GetLocalTemplatesAsync();

    /// <summary>
    /// Checks if there are any recently used templates in the history.
    /// </summary>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation. The task result contains
    /// <see langword="true"/> if there are templates in the usage history; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> HasRecentlyUsedTemplatesAsync();

    /// <summary>
    /// Gets an observable sequence of recently used templates.
    /// </summary>
    /// <returns>
    /// An <see cref="IObservable{T}"/> of <see cref="ITemplateInfo"/> representing
    /// recently used templates, ordered by most recent usage.
    /// </returns>
    public IObservable<ITemplateInfo> GetRecentlyUsedTemplates();
}
