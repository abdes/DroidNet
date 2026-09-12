// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns preview suspension while validated roots are replaced or restored.</summary>
public interface ICookPublicationPreview : IAsyncDisposable
{
    /// <summary>Pauses rendering, drains content work, and releases mounted readers before filesystem replacement.</summary>
    /// <returns>Completion after conflicting native reads and file handles have drained.</returns>
    public Task PrepareReplacementAsync();

    /// <summary>Mounts the complete root set and refreshes current reference intents while preview remains paused.</summary>
    /// <param name="roots">All roots belonging to the installed or restored publication.</param>
    /// <param name="writer">The writer granting read ownership, or null when restoring unchanged output after a busy result.</param>
    /// <returns>Completion after native bindings settle.</returns>
    public Task MountAsync(IReadOnlyList<string> roots, CookOutputWriteLease? writer);

    /// <summary>Resumes the current authoring preview after metadata and native bindings agree.</summary>
    /// <returns>Completion of the preview transition.</returns>
    public Task ResumeAsync();
}
