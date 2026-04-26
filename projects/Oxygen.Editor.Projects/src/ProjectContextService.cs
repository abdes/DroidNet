// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Host-level active project context service.
/// </summary>
public sealed class ProjectContextService : IProjectContextService
{
    private readonly Lock syncLock = new();
    private readonly List<IObserver<ProjectContext?>> observers = [];

    /// <inheritdoc/>
    public ProjectContext? ActiveProject { get; private set; }

    /// <inheritdoc/>
    public IObservable<ProjectContext?> ProjectChanged => new ReplayObservable(this);

    /// <inheritdoc/>
    public void Activate(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        this.Publish(context);
    }

    /// <inheritdoc/>
    public void Close() => this.Publish(context: null);

    private void Publish(ProjectContext? context)
    {
        IObserver<ProjectContext?>[] snapshot;
        lock (this.syncLock)
        {
            this.ActiveProject = context;
            snapshot = [.. this.observers];
        }

        foreach (var observer in snapshot)
        {
            observer.OnNext(context);
        }
    }

    private Subscription Subscribe(IObserver<ProjectContext?> observer)
    {
        ArgumentNullException.ThrowIfNull(observer);

        ProjectContext? activeProject;
        lock (this.syncLock)
        {
            this.observers.Add(observer);
            activeProject = this.ActiveProject;
        }

        observer.OnNext(activeProject);
        return new Subscription(this, observer);
    }

    private void Unsubscribe(IObserver<ProjectContext?> observer)
    {
        lock (this.syncLock)
        {
            _ = this.observers.Remove(observer);
        }
    }

    private sealed class ReplayObservable(ProjectContextService owner) : IObservable<ProjectContext?>
    {
        public IDisposable Subscribe(IObserver<ProjectContext?> observer) => owner.Subscribe(observer);
    }

    private sealed class Subscription(ProjectContextService owner, IObserver<ProjectContext?> observer) : IDisposable
    {
        private ProjectContextService? owner = owner;

        public void Dispose()
        {
            var target = Interlocked.Exchange(ref this.owner, null);
            target?.Unsubscribe(observer);
        }
    }
}
