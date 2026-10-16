// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>A preview that records the project roots of each publication it is told was committed.</summary>
internal sealed class RecordingPublicationPreview : ICookPublicationPreview
{
    public bool IsRuntimeAvailable => true;

    /// <summary>Gets the project mount names of each committed publication, in order.</summary>
    public List<IReadOnlyList<string>> Committed { get; } = [];

    public Task PrepareReplacementAsync() => Task.CompletedTask;

    public Task MountAsync(CookedContentMountSet mounts)
    {
        mounts.Dispose();
        return Task.CompletedTask;
    }

    public Task ResumeAsync() => Task.CompletedTask;

    public Task CommittedAsync(CookPublicationReadLease publication)
    {
        this.Committed.Add([.. publication.ProjectRoots.Keys.Order(StringComparer.Ordinal)]);
        return Task.CompletedTask;
    }

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
