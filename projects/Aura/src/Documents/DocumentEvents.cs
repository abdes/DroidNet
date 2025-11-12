// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;

#pragma warning disable SA1649 // File name should match first type name
#pragma warning disable SA1402 // File may only contain a single type

namespace DroidNet.Aura.Documents;

/// <summary>
///     Event arguments for DocumentOpened.
/// </summary>
public sealed class DocumentOpenedEventArgs(WindowContext? window, IDocumentMetadata metadata, int indexHint, bool shouldSelect) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public IDocumentMetadata Metadata { get; } = metadata;

    public int IndexHint { get; } = indexHint;

    public bool ShouldSelect { get; } = shouldSelect;
}

/// <summary>
///     Event arguments used when a document is about to close. Handlers can register
///     async veto tasks; the service should respect any veto.
/// </summary>
public sealed class DocumentClosingEventArgs(WindowContext? window, IDocumentMetadata metadata, bool force) : EventArgs
{
    private readonly List<Task<bool>> vetoTasks = [];

    public WindowContext? Window { get; } = window;

    public IDocumentMetadata Metadata { get; } = metadata;

    public bool Force { get; } = force;

    /// <summary>
    ///     Adds an asynchronous veto task. The task should return true to approve the close,
    ///     false to veto. The service implementation should await all registered veto tasks
    ///     and cancel the close if any veto.
    /// </summary>
    public void AddVetoTask(Task<bool> vetoTask)
        => this.vetoTasks.Add(vetoTask);

    internal Task<bool[]> GetVetoTasks() => Task.WhenAll(this.vetoTasks);
}

public sealed class DocumentClosedEventArgs(WindowContext? window, IDocumentMetadata metadata) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public IDocumentMetadata Metadata { get; } = metadata;
}

public sealed class DocumentDetachedEventArgs(WindowContext? window, IDocumentMetadata metadata) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public IDocumentMetadata Metadata { get; } = metadata;
}

public sealed class DocumentAttachedEventArgs(WindowContext? window, IDocumentMetadata metadata, int indexHint) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public IDocumentMetadata Metadata { get; } = metadata;

    public int IndexHint { get; } = indexHint;
}

public sealed class DocumentMetadataChangedEventArgs(WindowContext? window, IDocumentMetadata newMetadata) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public IDocumentMetadata NewMetadata { get; } = newMetadata;
}

public sealed class DocumentActivatedEventArgs(WindowContext? window, Guid documentId) : EventArgs
{
    public WindowContext? Window { get; } = window;

    public Guid DocumentId { get; } = documentId;
}
