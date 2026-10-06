// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class FailingPublicationPreview : ICookPublicationPreview
{

    public bool IsRuntimeAvailable => true;

    public int Mounts { get; private set; }

    public Task PrepareReplacementAsync() => Task.CompletedTask;
    public Task MountAsync(global::Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountSet mounts)
    {
        mounts.Dispose();
        this.Mounts++;
        return this.Mounts == 1 ? Task.FromException(new IOException("Injected preview failure")) : Task.CompletedTask;
    }

    public Task ResumeAsync() => Task.CompletedTask;

    public Task CommittedAsync(CookPublicationReadLease publication) => Task.CompletedTask;

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
