// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Keeps the active project's scripts analyzed through the engine's analyzer, analyzing a script again when its source
/// changes, and reads the scripts of the libraries the project mounts.
/// </summary>
/// <remarks>
/// Analysis is not cooking: it reads declarations and problems without running or compiling the script, so parameters
/// and problems update on save. It runs on the thread pool, and waits until the analyzer is available, so a project
/// opened before the engine SDK is verified is analyzed as soon as it is. Type checking runs when the SDK's API
/// definitions are available; otherwise scripts are linted only. The per-project state lives in a
/// <see cref="ProjectScriptFacts"/>, replaced when the project changes.
/// </remarks>
public sealed partial class ScriptFacts : IScripts, IDisposable
{
    private readonly IFileSystem fileSystem;
    private readonly IScriptAnalyzer analyzer;
    private readonly IScriptLibrary library;
    private readonly IOperationResultPublisher operationResults;
    private readonly ScriptToolingPaths paths;
    private readonly ILoggerFactory? loggerFactory;
    private readonly ILogger logger;
    private readonly SubscriberEvent<EventHandler<ScriptChangedEventArgs>> changed = new();
    private readonly SubscriberEvent<EventHandler> problemsChanged = new();
    private readonly Lock gate = new();
    private readonly Lazy<string?> definitions;
    private readonly IDisposable catalogChanges;
    private readonly IProjectSession session;
    private ProjectScriptFacts? current;
    private ScriptSnapshot snapshot = ScriptSnapshot.Empty;
    private int unavailableReported;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ScriptFacts"/> class and starts analyzing the session
    /// project's scripts.</summary>
    /// <param name="session">The project session, whose content mounts hold the scripts.</param>
    /// <param name="catalog">The project catalog, whose file changes analyze a script again.</param>
    /// <param name="fileSystem">The file system that holds the SDK's API definitions; the project's scripts are read
    /// through the project's own file system.</param>
    /// <param name="analyzer">The engine's script analyzer.</param>
    /// <param name="library">The scripts of mounted libraries.</param>
    /// <param name="operationResults">The publisher that reports an analyzer that cannot run.</param>
    /// <param name="paths">The SDK scripting files.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public ScriptFacts(
        IProjectSession session,
        IAssetCatalog catalog,
        IFileSystem fileSystem,
        IScriptAnalyzer analyzer,
        IScriptLibrary library,
        IOperationResultPublisher operationResults,
        ScriptToolingPaths paths,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(session);
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(analyzer);
        ArgumentNullException.ThrowIfNull(library);
        ArgumentNullException.ThrowIfNull(operationResults);
        ArgumentNullException.ThrowIfNull(paths);
        this.session = session;
        this.fileSystem = fileSystem;
        this.analyzer = analyzer;
        this.library = library;
        this.operationResults = operationResults;
        this.paths = paths;
        this.loggerFactory = loggerFactory;
        this.logger = loggerFactory?.CreateLogger<ScriptFacts>() ?? NullLogger<ScriptFacts>.Instance;
        this.definitions = new(this.ReadDefinitions);
        this.catalogChanges = catalog.Changes.Subscribe(this.OnCatalogChange);

        // The session's project is analyzed at once, and again in its new content mounts after each change.
        this.Follow(session.Context);
        session.ContextChanged += this.OnContextChanged;
    }

    /// <inheritdoc />
    public event EventHandler<ScriptChangedEventArgs>? Changed
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
    public IReadOnlyCollection<Script> All => Volatile.Read(ref this.snapshot).All;

    /// <inheritdoc />
    public IReadOnlyList<AssetProblem> Problems => Volatile.Read(ref this.snapshot).Problems;

    /// <inheritdoc />
    public Script? Find(AssetPath script)
    {
        ArgumentNullException.ThrowIfNull(script);
        return Volatile.Read(ref this.snapshot).Find(script);
    }

    /// <inheritdoc />
    public async Task<ScriptRead> ReadLibraryAsync(AssetPath script, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(script);
        if (Volatile.Read(ref this.current)?.Project is not { } project)
        {
            return ScriptRead.NotFound;
        }

        try
        {
            return await this.library.ReadAsync(project, script, cancellationToken).ConfigureAwait(false) is { } read
                ? ScriptRead.Found(read)
                : ScriptRead.NotFound;
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or InvalidOperationException)
        {
            this.LogLibraryScriptUnreadable(script, exception);
            return ScriptRead.Unavailable(exception.Message);
        }
    }

    /// <inheritdoc />
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed check is logged; the facts stay as they were, and the cook reports what is wrong with the file.")]
    public async Task CheckAsync(IReadOnlyCollection<AssetPath> sources, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sources);
        if (!this.analyzer.IsAvailable || Volatile.Read(ref this.current) is not { } facts)
        {
            return;
        }

        try
        {
            await facts.CheckAsync(sources, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (OperationCanceledException)
        {
            // The project closed.
        }
        catch (Exception exception)
        {
            this.LogScriptCheckFailed(sources.Count, exception);
        }
    }

    /// <inheritdoc />
    public Task RefreshAsync(bool force, CancellationToken cancellationToken = default)
        => Volatile.Read(ref this.current) is { } facts
            ? Task.Run(() => this.ScanAsync(facts, force, cancellationToken), cancellationToken)
            : Task.CompletedTask;

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

        this.session.ContextChanged -= this.OnContextChanged;
        this.catalogChanges.Dispose();
    }

    private string? ReadDefinitions()
    {
        try
        {
            return this.fileSystem.File.ReadAllText(this.paths.DefinitionsPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogDefinitionsMissing(this.paths.DefinitionsPath);
            return null;
        }
    }

    private void OnContextChanged(object? sender, EventArgs args) => this.Follow(this.session.Context);

    private void Follow(ProjectContext project)
    {
        ProjectScriptFacts started;
        lock (this.gate)
        {
            if (this.disposed || ReferenceEquals(project, this.current?.Project))
            {
                return;
            }

            // Disposing cancels the analyses in progress, so the earlier mounts publish nothing after this.
            this.current?.Dispose();
            this.current = new ProjectScriptFacts(project, this.analyzer, () => this.definitions.Value, this.Publish, this.ReportUnavailable, this.loggerFactory);
            this.Publish(ScriptSnapshot.Empty, script: null);
            started = this.current;
        }

        _ = Task.Run(() => this.ScanAsync(started, force: false, CancellationToken.None));
    }

    // The scan of an opened project and Check Scripts both end here: a failure is logged, never thrown at the caller.
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed scan is logged; saving a script analyzes it again.")]
    private async Task ScanAsync(ProjectScriptFacts facts, bool force, CancellationToken cancellationToken)
    {
        try
        {
            await facts.ScanAsync(force, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // The project closed, or the caller cancelled.
        }
        catch (Exception exception)
        {
            this.LogProjectScanFailed(facts.Project.ProjectRoot, exception);
        }
    }

    private void OnCatalogChange(AssetChange change)
    {
        var facts = Volatile.Read(ref this.current);
        if (facts is not null && (IsScript(change.Identity) || IsScript(change.PreviousIdentity)))
        {
            _ = Task.Run(() => this.UpdateAsync(facts, change));
        }

        static bool IsScript(AssetPath? path) => path is not null && AssetFileTypes.Is(path.Name, AssetFileCategory.Script);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed analysis of a changed file is logged; its next change analyzes it again.")]
    private async Task UpdateAsync(ProjectScriptFacts facts, AssetChange change)
    {
        try
        {
            await facts.UpdateAsync(change).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // The project closed.
        }
        catch (Exception exception)
        {
            this.LogScriptAnalysisFailed(change.Identity, exception);
        }
    }

    private void ReportUnavailable(string reason)
    {
        if (Interlocked.Exchange(ref this.unavailableReported, 1) != 0)
        {
            return;
        }

        this.LogAnalyzerUnavailable(reason);
        ScriptOperationResults.PublishFailure(
            this.operationResults,
            ScriptOperationKinds.Analyze,
            ScriptDiagnosticCodes.AnalysisUnavailable,
            "Scripts cannot be checked",
            "The engine script analyzer could not be loaded, so script problems and parameters are not shown. Cooking still reports script errors.");
    }

    private void Publish(ScriptSnapshot published, AssetPath? script)
    {
        Volatile.Write(ref this.snapshot, published);
        var args = new ScriptChangedEventArgs(script);
        this.changed.Invoke(handler => handler(this, args));
        this.problemsChanged.Invoke(handler => handler(this, EventArgs.Empty));
    }
}
