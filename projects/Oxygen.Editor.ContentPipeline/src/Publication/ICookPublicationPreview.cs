// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns preview suspension while validated roots are replaced or restored.</summary>
public interface ICookPublicationPreview : IAsyncDisposable
{
    /// <summary>Gets a value indicating whether native rendering participates in this publication.</summary>
    public bool IsRuntimeAvailable { get; }

    /// <summary>Pauses rendering and drains content work while retaining the prior accepted roots.</summary>
    /// <returns>Completion after outstanding scene requests have settled.</returns>
    public Task PrepareReplacementAsync();

    /// <summary>Mounts the complete root set and refreshes current reference intents while preview remains paused.</summary>
    /// <param name="mounts">Prepared admission and reader ownership, transferred to the preview even on failure.</param>
    /// <returns>Completion after native bindings settle.</returns>
    public Task MountAsync(Mounting.CookedContentMountSet mounts);

    /// <summary>Resumes the current authoring preview after metadata and native bindings agree.</summary>
    /// <returns>Completion of the preview transition.</returns>
    public Task ResumeAsync();

    /// <summary>Refreshes derived catalogs after durable commit and selection-gate release.</summary>
    /// <param name="publication">The borrowed committed snapshot.</param>
    /// <returns>Completion of catalog and observer updates.</returns>
    public Task CommittedAsync(CookPublicationReadLease publication);

}
