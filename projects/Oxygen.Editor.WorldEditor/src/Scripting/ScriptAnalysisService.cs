// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using DroidNet.Hosting.WinUI;
using Json.Schema;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scripting;
using Oxygen.Interop.Scripting;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Scripting;

/// <summary>
/// Keeps the active project's scripts analyzed through the engine analyzer, re-analyzing a script when its file
/// changes.
/// </summary>
/// <remarks>
/// Analysis runs off the UI thread; <see cref="Changes"/> are raised on the UI thread. A script whose content is
/// unchanged is not analyzed again. When two analyses of one file overlap, only the latest is published.
/// <para>
/// The analyzer is native and loads from the engine SDK, which the engine service makes loadable once it has verified
/// it. Analysis waits for that, so a project opened before the engine is ready is analyzed as soon as it is.
/// </para>
/// <para>
/// Hand-written input files are checked the same way, in managed code: JSON, the engine schema and self-contained
/// action references.
/// </para>
/// </remarks>
public sealed partial class ScriptAnalysisService : IScriptAnalysisService, IDisposable
{
    private readonly IProjectContextService projects;
    private readonly IOperationResultPublisher operationResults;
    private readonly ScriptToolingPaths paths;
    private readonly ILogger logger;
    private readonly Subject<ScriptAnalysisSnapshot> changes = new();
    private readonly ConcurrentDictionary<string, Entry> entries = new(StringComparer.OrdinalIgnoreCase);
    private readonly ConcurrentDictionary<string, long> versions = new(StringComparer.OrdinalIgnoreCase);
    private readonly IDisposable catalogChanges;
    private readonly IDisposable projectChanges;
    private readonly CancellationTokenSource lifetime = new();
    private readonly Lazy<string?> definitions;
    private readonly IEngineService engine;
    private readonly TaskCompletionSource nativeReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly ConcurrentDictionary<string, JsonSchema?> inputSchemas = new(StringComparer.OrdinalIgnoreCase);
    private int unavailableReported;
    private string? scannedProjectRoot;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ScriptAnalysisService"/> class and starts analyzing the
    /// active project's scripts.</summary>
    /// <param name="projects">The active project.</param>
    /// <param name="catalog">The project catalog, whose file changes trigger analysis.</param>
    /// <param name="hosting">The hosting context whose dispatcher raises <see cref="Changes"/>.</param>
    /// <param name="operationResults">The operation-result publisher for an unavailable analyzer.</param>
    /// <param name="engine">The engine service, which makes the native SDK loadable.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    /// <param name="paths">The SDK scripting files; discovered from the editor installation when omitted.</param>
    public ScriptAnalysisService(
        IProjectContextService projects,
        IAssetCatalog catalog,
        HostingContext hosting,
        IOperationResultPublisher operationResults,
        IEngineService engine,
        ILoggerFactory? loggerFactory = null,
        ScriptToolingPaths? paths = null)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentNullException.ThrowIfNull(hosting);
        ArgumentNullException.ThrowIfNull(engine);
        this.projects = projects;
        this.engine = engine;
        engine.StateChanged += this.OnEngineStateChanged;
        this.OnEngineStateChanged(engine.State);
        this.operationResults = operationResults;
        this.paths = paths ?? ScriptToolingPaths.Discover();
        this.logger = loggerFactory?.CreateLogger<ScriptAnalysisService>() ?? NullLogger<ScriptAnalysisService>.Instance;
        this.definitions = new(this.ReadDefinitions);
        this.Changes = this.changes.ObserveOn(hosting.DispatcherScheduler);
        this.catalogChanges = catalog.Changes.Subscribe(this.OnCatalogChange);

        // The workspace can exist before its project is active; scan whenever a project becomes active. The stream
        // replays the current project, so an already active one is scanned at once.
        this.projectChanges = projects.ProjectChanged.Subscribe(this.OnProjectChanged);
    }

    /// <inheritdoc />
    public IObservable<ScriptAnalysisSnapshot> Changes { get; }

    /// <inheritdoc />
    public IReadOnlyCollection<ScriptAnalysisSnapshot> Current => [.. this.entries.Values.Select(static entry => entry.Snapshot)];

    /// <inheritdoc />
    public ScriptAnalysisSnapshot? Find(string sourcePath)
        => this.entries.TryGetValue(Path.GetFullPath(sourcePath), out var entry) ? entry.Snapshot : null;

    /// <inheritdoc />
    public Task<ScriptAnalysisSnapshot?> AnalyzeAsync(string sourcePath, CancellationToken cancellationToken = default)
        => this.AnalyzeAsync(sourcePath, force: false, cancellationToken);

    /// <inheritdoc />
    public Task RefreshAsync(CancellationToken cancellationToken = default)
        => Task.Run(() => this.AnalyzeProjectAsync(force: true, cancellationToken), cancellationToken);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.lifetime.Cancel();
        this.engine.StateChanged -= this.OnEngineStateChanged;
        this.projectChanges.Dispose();
        this.catalogChanges.Dispose();
        this.changes.OnCompleted();
        this.changes.Dispose();
        this.lifetime.Dispose();
    }

    /// <summary>Maps an engine analysis to the editor's snapshot.</summary>
    /// <param name="path">The script file.</param>
    /// <param name="scriptUri">The script's asset identity.</param>
    /// <param name="analysis">The engine analysis.</param>
    /// <param name="lastValid">The parameters of the last analysis whose declaration was valid.</param>
    /// <returns>The snapshot; when the declaration has errors, it carries <paramref name="lastValid"/> as out of date.</returns>
    internal static ScriptAnalysisSnapshot CreateSnapshot(string path, Uri scriptUri, ScriptAnalysisResult analysis, IReadOnlyList<ScriptParameter> lastValid)
    {
        ArgumentNullException.ThrowIfNull(analysis);
        var problems = analysis.Diagnostics.Select(diagnostic => new ScriptProblem(
            diagnostic.Severity switch
            {
                ScriptDiagnosticSeverity.Error => ScriptProblemSeverity.Error,
                ScriptDiagnosticSeverity.Warning => ScriptProblemSeverity.Warning,
                _ => ScriptProblemSeverity.Note,
            },
            diagnostic.Code,
            diagnostic.Message,
            path,
            diagnostic.PrimarySpan?.Line,
            diagnostic.PrimarySpan?.Column,
            diagnostic.Rendered,
            diagnostic.Explanation)).ToList();
        var parameters = analysis.Parameters.Select(static parameter => new ScriptParameter(
            parameter.Name,
            (ScriptParameterKind)(int)parameter.Type,
            parameter.DefaultValue switch
            {
                Vector2 or Vector3 or Vector4 or bool or int or float or string => parameter.DefaultValue,
                _ => null,
            },
            parameter.AssetType,
            parameter.Min,
            parameter.Max,
            parameter.Step,
            parameter.Unit ?? string.Empty,
            parameter.Description ?? string.Empty,
            parameter.Options ?? [],
            parameter.IsColor)).ToList();

        // A declaration with errors yields no parameters; keep showing the last valid ones, marked out of date.
        var declarationBroken = parameters.Count == 0 && analysis.HasDeclaration && problems.Any(static problem => problem.Severity == ScriptProblemSeverity.Error);
        var outOfDate = declarationBroken && lastValid.Count > 0;
        return new ScriptAnalysisSnapshot(path, scriptUri, problems, outOfDate ? lastValid : parameters, outOfDate);
    }

    /// <summary>Finds the other script sources in a script's folder that cook to the same asset.</summary>
    /// <param name="path">The script file.</param>
    /// <returns>The file names of the colliding sources, sorted.</returns>
    internal static IReadOnlyList<string> FindCollisions(string path)
    {
        var folder = Path.GetDirectoryName(path);
        if (folder is null || !Directory.Exists(folder))
        {
            return [];
        }

        var stem = AssetFileTypes.RemoveSuffix(Path.GetFileName(path));
        return [.. Directory.EnumerateFiles(folder)
            .Where(other => AssetFileTypes.Is(other, AssetFileCategory.Script)
                && !string.Equals(other, path, StringComparison.OrdinalIgnoreCase)
                && string.Equals(AssetFileTypes.RemoveSuffix(Path.GetFileName(other)), stem, StringComparison.OrdinalIgnoreCase))
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.OrdinalIgnoreCase)];
    }

    private static IEnumerable<string> EnumerateScripts(string root)
    {
        if (!Directory.Exists(root))
        {
            return [];
        }

        var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = FileAttributes.Hidden | FileAttributes.System };
        return Directory.EnumerateFiles(root, "*", options)
            .Where(static path => AssetFileTypes.Is(path, AssetFileCategory.Script) || AssetFileTypes.Is(path, AssetFileCategory.Input))
            .Where(path => !Path.GetRelativePath(root, path).Split(Path.DirectorySeparatorChar).Any(static segment => segment.StartsWith('.')));
    }

    private static ScriptProblem CollisionProblem(string path, IReadOnlyList<string> collisions)
    {
        var name = Path.GetFileName(path);
        var others = string.Join(", ", collisions);
        var message = $"{name} and {others} cook to the same script; rename one of them";
        return new ScriptProblem(
            ScriptProblemSeverity.Error,
            ScriptDiagnosticCodes.IdentityCollision,
            message,
            path,
            Line: null,
            Column: null,
            Rendered: $"error[{ScriptDiagnosticCodes.IdentityCollision}]: {message}\n --> {path}\n",
            Explanation: "A script is identified by its folder and file name without the extension, so two sources such as move.lua and move.luau in one folder would produce the same asset. Neither is cooked until one is renamed or removed.");
    }

    private static string? ToScriptPath(ProjectContext project, Uri uri)
    {
        var virtualPath = AssetUriHelper.GetVirtualPath(uri);
        return (AssetFileTypes.Is(virtualPath, AssetFileCategory.Script) || AssetFileTypes.Is(virtualPath, AssetFileCategory.Input))
            && RelocationPaths.ToPhysical(project, virtualPath) is { } physical ? physical : null;
    }

    /// <summary>Analyzes a script, unless its content is unchanged and <paramref name="force"/> is not set.</summary>
    /// <param name="sourcePath">The script file.</param>
    /// <param name="force">Whether to analyze even when the content is unchanged.</param>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>The analysis, or <see langword="null"/> when the file is not a script of the active project.</returns>
    private async Task<ScriptAnalysisSnapshot?> AnalyzeAsync(string sourcePath, bool force, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        var path = Path.GetFullPath(sourcePath);
        if (this.projects.ActiveProject is not { } project || RelocationPaths.ToVirtual(project, path) is not { } virtualPath)
        {
            return null;
        }

        if (AssetFileTypes.Is(path, AssetFileCategory.Input))
        {
            return await this.CheckInputFileAsync(path, virtualPath, force, cancellationToken).ConfigureAwait(false);
        }

        if (!AssetFileTypes.Is(path, AssetFileCategory.Script))
        {
            return null;
        }

        var version = this.versions.AddOrUpdate(path, 1, static (_, current) => current + 1);
        byte[] bytes;
        try
        {
            bytes = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
        {
            this.Remove(path);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // The editor that saved the file may still hold it; the next change notification analyzes it again.
            this.LogScriptUnreadable(path, exception);
            return this.Find(path);
        }

        var collisions = FindCollisions(path);
        var hash = Convert.ToHexString(SHA256.HashData(bytes)) + "|" + string.Join('|', collisions);
        if (this.entries.TryGetValue(path, out var previous) && !force && string.Equals(previous.Hash, hash, StringComparison.Ordinal))
        {
            return previous.Snapshot;
        }

        await this.nativeReady.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        var sourceName = Path.GetRelativePath(project.ProjectRoot, path).Replace('\\', '/');
        var scriptUri = new Uri($"{AssetUris.Scheme}://{AssetFileTypes.ToCookedPath(virtualPath) ?? virtualPath}");
        var snapshot = this.Run(path, sourceName, bytes, scriptUri, previous?.LastValidParameters ?? []);
        if (snapshot is null || (this.versions.TryGetValue(path, out var latest) && latest != version))
        {
            return this.Find(path);
        }

        if (collisions.Count > 0)
        {
            snapshot = snapshot with { Problems = [CollisionProblem(path, collisions), .. snapshot.Problems] };
        }

        this.entries[path] = new Entry(hash, snapshot, snapshot.ParametersOutOfDate ? previous?.LastValidParameters ?? [] : snapshot.Parameters);
        this.changes.OnNext(snapshot);
        return snapshot;
    }

    private async Task<ScriptAnalysisSnapshot?> CheckInputFileAsync(string path, string virtualPath, bool force, CancellationToken cancellationToken)
    {
        byte[] bytes;
        try
        {
            bytes = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
        {
            this.Remove(path);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogScriptUnreadable(path, exception);
            return this.Find(path);
        }

        var hash = Convert.ToHexString(SHA256.HashData(bytes));
        if (this.entries.TryGetValue(path, out var previous) && !force && string.Equals(previous.Hash, hash, StringComparison.Ordinal))
        {
            return previous.Snapshot;
        }

        var schema = this.inputSchemas.GetOrAdd(
            path.EndsWith(AssetFileTypes.InputActionFile, StringComparison.OrdinalIgnoreCase) ? "oxygen.input-action.schema.json" : "oxygen.input.schema.json",
            this.LoadInputSchema);
        var problems = InputFileCheck.Check(path, System.Text.Encoding.UTF8.GetString(bytes), schema);
        var snapshot = new ScriptAnalysisSnapshot(path, new Uri($"{AssetUris.Scheme}://{virtualPath}"), problems, [], ParametersOutOfDate: false);
        this.entries[path] = new Entry(hash, snapshot, []);
        this.changes.OnNext(snapshot);
        return snapshot;
    }

    private JsonSchema? LoadInputSchema(string fileName)
    {
        try
        {
            // The catalog resolves the schema's reference to the shared name definition beside it.
            return EditorSchemaCatalog.LoadFromDirectory(this.paths.SchemaDirectory).GetValidationSchema(fileName);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or KeyNotFoundException or System.Text.Json.JsonException or JsonSchemaException)
        {
            this.LogInputSchemaUnavailable(fileName, exception);
            return null;
        }
    }

    // Loading the Interop assembly fails when the method that uses its types is compiled, so those types stay in
    // AnalyzeNative and the load failure surfaces here, at its call.
    private ScriptAnalysisSnapshot? Run(string path, string sourceName, byte[] bytes, Uri scriptUri, IReadOnlyList<ScriptParameter> lastValid)
    {
        try
        {
            return this.AnalyzeNative(path, sourceName, bytes, scriptUri, lastValid);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DllNotFoundException or BadImageFormatException or TypeLoadException or FileLoadException or System.Runtime.InteropServices.SEHException)
        {
            if (Interlocked.Exchange(ref this.unavailableReported, 1) == 0)
            {
                ScriptOperationResults.PublishFailure(
                    this.operationResults,
                    ScriptOperationKinds.Analyze,
                    ScriptDiagnosticCodes.AnalysisUnavailable,
                    "Scripts cannot be checked",
                    "The engine script analyzer could not be loaded, so script problems and parameters are not shown. Cooking still reports script errors.",
                    exception: exception);
            }

            return null;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private ScriptAnalysisSnapshot AnalyzeNative(string path, string sourceName, byte[] bytes, Uri scriptUri, IReadOnlyList<ScriptParameter> lastValid)
    {
        var definitionsText = this.definitions.Value;
        var analysis = ScriptAnalysis.Analyze(sourceName, bytes, definitionsText is null ? ScriptAnalysisLevel.Lint : ScriptAnalysisLevel.Types, definitionsText);
        return CreateSnapshot(path, scriptUri, analysis, lastValid);
    }

    private void OnEngineStateChanged(object? sender, EngineStateChangedEventArgs args) => this.OnEngineStateChanged(args.State);

    private void OnEngineStateChanged(EngineServiceState state)
    {
        // Ready and later: the engine service has verified the SDK and put its binaries on the search path.
        if (state is EngineServiceState.Ready or EngineServiceState.Starting or EngineServiceState.Running)
        {
            _ = this.nativeReady.TrySetResult();
        }
    }

    private string? ReadDefinitions()
    {
        try
        {
            return File.ReadAllText(this.paths.DefinitionsPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            this.LogDefinitionsMissing(this.paths.DefinitionsPath);
            return null;
        }
    }

    private void OnProjectChanged(ProjectContext? project)
    {
        if (this.disposed || string.Equals(project?.ProjectRoot, this.scannedProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        this.scannedProjectRoot = project?.ProjectRoot;
        foreach (var path in this.entries.Keys.ToArray())
        {
            this.Remove(path);
        }

        if (project is not null)
        {
            _ = Task.Run(() => this.AnalyzeProjectAsync(force: false, this.lifetime.Token));
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed scan is logged; saving a script analyzes it again.")]
    private async Task AnalyzeProjectAsync(bool force, CancellationToken cancellationToken)
    {
        if (this.projects.ActiveProject is not { } project)
        {
            return;
        }

        try
        {
            var scripts = RelocationPaths.GetWritableMounts(project)
                .SelectMany(mount => EnumerateScripts(Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath))))
                .ToList();
            this.LogProjectScan(project.ProjectRoot, scripts.Count);
            foreach (var path in scripts)
            {
                cancellationToken.ThrowIfCancellationRequested();
                _ = await this.AnalyzeAsync(path, force, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
            // The workspace closed.
        }
        catch (Exception exception)
        {
            this.LogProjectScanFailed(project.ProjectRoot, exception);
        }
    }

    private void OnCatalogChange(AssetChange change)
    {
        if (this.disposed || this.projects.ActiveProject is not { } project)
        {
            return;
        }

        if (change.Kind == AssetChangeKind.Relocated && change.PreviousUri is { } previous && ToScriptPath(project, previous) is { } old)
        {
            this.Remove(old);
        }

        if (ToScriptPath(project, change.Uri) is not { } path)
        {
            return;
        }

        if (change.Kind == AssetChangeKind.Removed)
        {
            this.Remove(path);
        }
        else
        {
            _ = Task.Run(() => this.AnalyzeChangedAsync(path));
        }

        if (!AssetFileTypes.Is(path, AssetFileCategory.Script))
        {
            return;
        }

        foreach (var sibling in this.entries.Keys.Where(other => !string.Equals(other, path, StringComparison.OrdinalIgnoreCase)
            && string.Equals(Path.GetDirectoryName(other), Path.GetDirectoryName(path), StringComparison.OrdinalIgnoreCase)
            && string.Equals(AssetFileTypes.RemoveSuffix(Path.GetFileName(other)), AssetFileTypes.RemoveSuffix(Path.GetFileName(path)), StringComparison.OrdinalIgnoreCase)).ToArray())
        {
            _ = Task.Run(() => this.AnalyzeChangedAsync(sibling));
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed analysis of a changed file is logged; its next change analyzes it again.")]
    private async Task AnalyzeChangedAsync(string path)
    {
        try
        {
            _ = await this.AnalyzeAsync(path, force: false, this.lifetime.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // The workspace closed.
        }
        catch (Exception exception)
        {
            this.LogScriptAnalysisFailed(path, exception);
        }
    }

    private void Remove(string path)
    {
        if (this.entries.TryRemove(path, out var entry) && !this.disposed)
        {
            this.changes.OnNext(entry.Snapshot with { Problems = [], IsRemoved = true });
        }
    }

    private sealed record Entry(string Hash, ScriptAnalysisSnapshot Snapshot, IReadOnlyList<ScriptParameter> LastValidParameters);
}
