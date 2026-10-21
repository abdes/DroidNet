// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace DroidNet.Composition;

/// <summary>
///     Owns one scope of a scope chain: its container, the scopes created under it and the async
///     disposables it must await.
/// </summary>
/// <remarks>
///     <para>
///     Disposal runs in this order: the child scopes, the most recently created first; then the owned
///     async disposables, the most recently owned first; then the scope's container, which disposes
///     what the scope created and never a parent's instance. DryIoc has no async disposal, so the
///     handle awaits the owned disposables itself. Disposal continues on the caller's context, because
///     a scope may own UI objects. Disposing a handle again does nothing.
///     </para>
///     <para>
///     The handle of the root scope wraps the host's container and never disposes it; the host does.
///     </para>
/// </remarks>
public sealed class ScopeHandle : IAsyncDisposable
{
    private readonly Lock gate = new();
    private readonly List<ScopeHandle> children = [];
    private readonly List<IAsyncDisposable> owned = [];
    private readonly bool ownsContainer;
    private int disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ScopeHandle" /> class.
    /// </summary>
    /// <param name="container">The scope's container.</param>
    /// <param name="parent">The handle of the parent scope, or <see langword="null" /> for the root.</param>
    /// <param name="ownsContainer">Whether disposing the handle disposes the container.</param>
    internal ScopeHandle(IContainer container, ScopeHandle? parent, bool ownsContainer)
    {
        this.Container = container;
        this.Parent = parent;
        this.ownsContainer = ownsContainer;
    }

    /// <summary>
    ///     Gets the handle of the parent scope, or <see langword="null" /> for the root.
    /// </summary>
    public ScopeHandle? Parent { get; }

    /// <summary>
    ///     Gets the resolver of the scope.
    /// </summary>
    public IResolver Resolver => this.Container;

    /// <summary>
    ///     Gets a value indicating whether the handle has been disposed.
    /// </summary>
    public bool IsDisposed => Volatile.Read(ref this.disposed) != 0;

    /// <summary>
    ///     Gets the scope's container.
    /// </summary>
    internal IContainer Container { get; }

    /// <summary>
    ///     Makes the handle await <paramref name="disposable" /> when it is disposed, after its child
    ///     scopes and before its container.
    /// </summary>
    /// <param name="disposable">The object the scope owns.</param>
    /// <exception cref="ObjectDisposedException">The handle has been disposed.</exception>
    public void Own(IAsyncDisposable disposable)
    {
        ArgumentNullException.ThrowIfNull(disposable);

        lock (this.gate)
        {
            ObjectDisposedException.ThrowIf(this.IsDisposed, this);
            this.owned.Add(disposable);
        }
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref this.disposed, 1) != 0)
        {
            return;
        }

        ScopeHandle[] childScopes;
        IAsyncDisposable[] ownedObjects;
        lock (this.gate)
        {
            childScopes = [.. this.children];
            ownedObjects = [.. this.owned];
            this.children.Clear();
            this.owned.Clear();
        }

        this.Parent?.Detach(this);

        // Continue on the caller's context: the scopes may own UI objects.
        for (var index = childScopes.Length - 1; index >= 0; index--)
        {
            await childScopes[index].DisposeAsync().ConfigureAwait(true);
        }

        for (var index = ownedObjects.Length - 1; index >= 0; index--)
        {
            await ownedObjects[index].DisposeAsync().ConfigureAwait(true);
        }

        if (this.ownsContainer)
        {
            this.Container.Dispose();
        }
    }

    /// <summary>
    ///     Makes a new scope a child of this one.
    /// </summary>
    /// <param name="child">The handle of the new scope.</param>
    /// <returns><see langword="false" /> when this handle has been disposed.</returns>
    internal bool TryAttach(ScopeHandle child)
    {
        lock (this.gate)
        {
            if (this.IsDisposed)
            {
                return false;
            }

            this.children.Add(child);
            return true;
        }
    }

    private void Detach(ScopeHandle child)
    {
        lock (this.gate)
        {
            _ = this.children.Remove(child);
        }
    }
}
