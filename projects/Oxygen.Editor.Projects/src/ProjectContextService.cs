// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Host-level active project context service.
/// </summary>
public sealed class ProjectContextService : IProjectContextService
{
    private readonly Lock syncLock = new();
    private readonly List<IObserver<ProjectContext?>> observers = [];
    private readonly List<IObserver<ProjectSettings>> settingsObservers = [];

    /// <inheritdoc/>
    public ProjectContext? ActiveProject { get; private set; }

    /// <inheritdoc/>
    public IObservable<ProjectContext?> ProjectChanged => new ReplayObservable(this);

    /// <inheritdoc/>
    public ProjectSettings Settings { get; private set; } = new();

    /// <inheritdoc/>
    public IObservable<ProjectSettings> SettingsChanged => new SettingsObservable(this);

    /// <inheritdoc/>
    public void Activate(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        this.Publish(context);
    }

    /// <inheritdoc/>
    public void Close() => this.Publish(context: null);

    /// <inheritdoc/>
    public void UpdateSettings(ProjectSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        IObserver<ProjectSettings>[] snapshot;
        lock (this.syncLock)
        {
            if (this.ActiveProject is null)
            {
                throw new InvalidOperationException("Settings belong to an active project.");
            }

            this.Settings = settings;
            snapshot = [.. this.settingsObservers];
        }

        foreach (var observer in snapshot)
        {
            observer.OnNext(settings);
        }
    }

    private void Publish(ProjectContext? context)
    {
        IObserver<ProjectContext?>[] snapshot;
        lock (this.syncLock)
        {
            this.ActiveProject = context;
            this.Settings = context?.Settings ?? new();
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

    private SettingsSubscription SubscribeSettings(IObserver<ProjectSettings> observer)
    {
        ArgumentNullException.ThrowIfNull(observer);
        lock (this.syncLock)
        {
            this.settingsObservers.Add(observer);
        }

        return new SettingsSubscription(this, observer);
    }

    private void UnsubscribeSettings(IObserver<ProjectSettings> observer)
    {
        lock (this.syncLock)
        {
            _ = this.settingsObservers.Remove(observer);
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
            var target = Interlocked.Exchange(ref this.owner, value: null);
            target?.Unsubscribe(observer);
        }
    }

    private sealed class SettingsObservable(ProjectContextService owner) : IObservable<ProjectSettings>
    {
        public IDisposable Subscribe(IObserver<ProjectSettings> observer) => owner.SubscribeSettings(observer);
    }

    private sealed class SettingsSubscription(ProjectContextService owner, IObserver<ProjectSettings> observer) : IDisposable
    {
        private ProjectContextService? owner = owner;

        public void Dispose()
        {
            var target = Interlocked.Exchange(ref this.owner, value: null);
            target?.UnsubscribeSettings(observer);
        }
    }
}
