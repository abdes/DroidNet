// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>Asks a person which models to import and how: the Import model dialog, which a UI surface such as the
/// Content Explorer opens without knowing the module that shows it.</summary>
public interface IModelImportPrompt
{
    /// <summary>Shows the import dialog and returns what the person chose to import.</summary>
    /// <param name="request">The files the dialog starts with and its destination folder.</param>
    /// <param name="cancellationToken">Closes the dialog.</param>
    /// <returns>The models and the run's settings, or <see langword="null"/> when the person cancelled.</returns>
    public Task<ModelImportPlan?> ShowAsync(ModelImportPromptRequest request, CancellationToken cancellationToken = default);
}
