// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// Keeps the active project's input sources read and checked, the way their cook will check them, and reads a source
/// again when its file changes.
/// </summary>
/// <remarks>
/// Reading runs on the thread pool. A change to one source checks every source again, since a source can name the
/// actions of another by path. The per-project state lives in a <see cref="ProjectInputFacts"/>, replaced when the
/// project changes.
/// </remarks>
public sealed partial class InputFacts : IInputSources, IDisposable
{
    private readonly ILoggerFactory? loggerFactory;
    private readonly ILogger logger;
    private readonly SubscriberEvent<EventHandler<InputSourceChangedEventArgs>> changed = new();
    private readonly SubscriberEvent<EventHandler> problemsChanged = new();
    private readonly Lock gate = new();
    private readonly IDisposable catalogChanges;
    private readonly IDisposable projectChanges;
    private ProjectInputFacts? current;
    private InputSnapshot snapshot = InputSnapshot.Empty;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="InputFacts"/> class and starts reading the active
    /// project's input sources.</summary>
    /// <param name="projects">The active project.</param>
    /// <param name="catalog">The project catalog, whose file changes read a source again.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public InputFacts(IProjectContextService projects, IAssetCatalog catalog, ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(projects);
        ArgumentNullException.ThrowIfNull(catalog);
        this.loggerFactory = loggerFactory;
        this.logger = loggerFactory?.CreateLogger<InputFacts>() ?? NullLogger<InputFacts>.Instance;
        this.catalogChanges = catalog.Changes.Subscribe(this.OnCatalogChange);

        // The stream replays the current project, so an open one is read at once.
        this.projectChanges = projects.ProjectChanged.Subscribe(this.OnProjectChanged);
    }

    /// <inheritdoc />
    public event EventHandler<InputSourceChangedEventArgs>? Changed
    {
        add => this.changed.Add(value);
        remove => this.changed.Remove(value);
    }

    /// <inheritdoc />
    public event EventHandler? ProblemsChanged
    {
        add => this.problemsChanged.Add(value);
        remove => this.problemsChanged.Remove(value);
    }

    /// <inheritdoc />
    public IReadOnlyCollection<InputContainer> All => Volatile.Read(ref this.snapshot).All;

    /// <inheritdoc />
    public IReadOnlyList<AssetProblem> Problems => Volatile.Read(ref this.snapshot).Problems;

    /// <inheritdoc />
    public InputContainer? Find(AssetPath container)
    {
        ArgumentNullException.ThrowIfNull(container);
        return Volatile.Read(ref this.snapshot).Find(container);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.gate)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.current?.Dispose();
            this.current = null;
        }

        this.projectChanges.Dispose();
        this.catalogChanges.Dispose();
    }

    private void OnProjectChanged(ProjectContext? project)
    {
        ProjectInputFacts? started;
        lock (this.gate)
        {
            if (this.disposed)
            {
                return;
            }

            // Disposing waits for a read in progress, so the closed project publishes nothing after this.
            this.current?.Dispose();
            this.current = project is null ? null : new ProjectInputFacts(project, this.Publish, this.loggerFactory);
            this.Publish(InputSnapshot.Empty, container: null);
            started = this.current;
        }

        if (started is not null)
        {
            _ = Task.Run(() => this.Scan(started));
        }
    }

    private void Scan(ProjectInputFacts facts)
    {
        try
        {
            facts.Scan();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogProjectScanFailed(facts.Project.ProjectRoot, exception);
        }
    }

    private void OnCatalogChange(AssetChange change)
    {
        var facts = Volatile.Read(ref this.current);
        if (facts is not null && (IsInputSource(change.Identity) || IsInputSource(change.PreviousIdentity)))
        {
            _ = Task.Run(() => facts.Update(change));
        }

        static bool IsInputSource(AssetPath? path) => path is not null && AssetFileTypes.Is(path.Name, AssetFileCategory.Input);
    }

    private void Publish(InputSnapshot published, AssetPath? container)
    {
        Volatile.Write(ref this.snapshot, published);
        var args = new InputSourceChangedEventArgs(container);
        this.changed.Invoke(handler => handler(this, args));
        this.problemsChanged.Invoke(handler => handler(this, EventArgs.Empty));
    }
}
