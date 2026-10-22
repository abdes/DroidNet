// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The scripts of one project: analyzed when the project opens, analyzed again when a source changes, and published
/// as a new <see cref="ScriptSnapshot"/> after each change.
/// </summary>
/// <remarks>
/// A source whose content is unchanged is not analyzed again. When two analyses of one source overlap, only the latest
/// is published. Nothing is published once the project is closed.
/// </remarks>
internal sealed partial class ProjectScriptFacts : IDisposable
{
    private readonly IScriptAnalyzer analyzer;
    private readonly Func<string?> definitions;
    private readonly Action<ScriptSnapshot, AssetPath?> publish;
    private readonly Action<string> reportUnavailable;
    private readonly ILogger logger;
    private readonly Lock gate = new();
    private readonly ConcurrentDictionary<AssetPath, long> versions = new();
    private readonly CancellationTokenSource lifetime = new();
    private readonly CancellationToken closed;
    private ImmutableDictionary<AssetPath, ScriptEntry> entries = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ProjectScriptFacts"/> class.</summary>
    /// <param name="project">The project.</param>
    /// <param name="analyzer">The script analyzer.</param>
    /// <param name="definitions">Reads the engine's API definitions, or <see langword="null"/> without them.</param>
    /// <param name="publish">Publishes a snapshot, with the script that changed or <see langword="null"/> for all.</param>
    /// <param name="reportUnavailable">Reports that the analyzer could not run, with why.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public ProjectScriptFacts(
        ProjectContext project,
        IScriptAnalyzer analyzer,
        Func<string?> definitions,
        Action<ScriptSnapshot, AssetPath?> publish,
        Action<string> reportUnavailable,
        ILoggerFactory? loggerFactory = null)
    {
        this.Project = project;
        this.analyzer = analyzer;
        this.definitions = definitions;
        this.publish = publish;
        this.reportUnavailable = reportUnavailable;
        this.logger = loggerFactory?.CreateLogger<ProjectScriptFacts>() ?? NullLogger<ProjectScriptFacts>.Instance;

        // Taken now: the source is disposed when the project closes, and its token stays usable once cancelled.
        this.closed = this.lifetime.Token;
    }

    /// <summary>Gets the project.</summary>
    public ProjectContext Project { get; }

    /// <summary>Maps an analysis to the script it describes.</summary>
    /// <param name="identity">The script's identity.</param>
    /// <param name="source">The script's source file.</param>
    /// <param name="analysis">The analysis.</param>
    /// <param name="lastValid">The parameters of the last analysis whose declaration was valid.</param>
    /// <returns>The script; when its declaration has errors, it carries <paramref name="lastValid"/> as out of
    /// date.</returns>
    public static Script CreateScript(AssetPath identity, FileSystemPath source, ScriptAnalysis analysis, IReadOnlyList<ScriptParameter> lastValid)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(analysis);
        ArgumentNullException.ThrowIfNull(lastValid);
        var problems = analysis.Problems.Select(problem => problem with { File = source.FullPath }).ToList();

        // A declaration with errors yields no parameters; keep showing the last valid ones, marked out of date.
        var declarationBroken = analysis.Parameters.Count == 0 && analysis.HasDeclaration
            && problems.Exists(static problem => problem.Severity >= DiagnosticSeverity.Error);
        var outOfDate = declarationBroken && lastValid.Count > 0;
        return new Script
        {
            Identity = identity,
            Origin = ScriptOrigin.Source,
            SourcePath = source,
            Parameters = outOfDate ? lastValid : analysis.Parameters,
            Problems = problems,
            ParametersOutOfDate = outOfDate,
        };
    }

    /// <summary>Analyzes every script source of the project's authoring mounts.</summary>
    /// <param name="force">Whether to analyze a source whose content has not changed.</param>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>A task that completes when every source has been analyzed.</returns>
    /// <exception cref="IOException">A folder of the project cannot be listed.</exception>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A script whose analysis fails is logged, and the scan goes on with the others.")]
    public async Task ScanAsync(bool force, CancellationToken cancellationToken)
    {
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.closed);
        var sources = this.EnumerateSources().ToList();
        this.LogProjectScan(sources.Count);
        foreach (var (identity, file) in sources)
        {
            try
            {
                await this.AnalyzeAsync(identity, file, force, cancellation.Token).ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                this.LogScriptScanFailed(file, exception);
            }
        }
    }

    /// <summary>Analyzes again the sources a catalog change names: the one it left and the one it made.</summary>
    /// <param name="change">The change.</param>
    /// <returns>A task that completes when they have been analyzed.</returns>
    public async Task UpdateAsync(AssetChange change)
    {
        ArgumentNullException.ThrowIfNull(change);
        foreach (var (identity, file) in new[] { change.PreviousIdentity, change.Identity }.Select(this.SourceOf).OfType<(AssetPath Identity, FileSystemPath File)>())
        {
            // The file is read whatever the change says: changes can arrive out of order, and the file is the truth.
            await this.AnalyzeAsync(identity, file, force: false, this.closed).ConfigureAwait(false);
        }
    }

    /// <summary>Analyzes the scripts of some source files whose content differs from their last analysis.</summary>
    /// <param name="sources">The source files, as the catalog names them; those that are not scripts are ignored.</param>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>A task that completes when they have been analyzed.</returns>
    public async Task CheckAsync(IEnumerable<AssetPath> sources, CancellationToken cancellationToken)
    {
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.closed);
        foreach (var (identity, file) in sources.Select(this.SourceOf).OfType<(AssetPath Identity, FileSystemPath File)>().Distinct())
        {
            await this.AnalyzeAsync(identity, file, force: false, cancellation.Token).ConfigureAwait(false);
        }
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
        }

        this.lifetime.Cancel();
        this.lifetime.Dispose();
    }

    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));

    private IEnumerable<(AssetPath Identity, FileSystemPath File)> EnumerateSources()
    {
        foreach (var mount in this.Project.AuthoringMounts.Where(static mount => !ProjectOutputFolders.Contains(mount)))
        {
            var root = this.Project.ProjectRoot.Combine(mount.RelativePath);
            if (!this.Project.FileSystem.Directory.Exists(root.FullPath))
            {
                continue;
            }

            foreach (var path in this.Project.FileSystem.Directory.EnumerateFiles(root.FullPath, "*", SearchOption.AllDirectories))
            {
                var file = FileSystemPath.From(path);
                if (!file.GetPathBelow(root).Split('/').Any(static segment => segment.StartsWith('.'))
                    && this.SourceOf(this.Project.FindByFilePath(file)?.Path) is { } source)
                {
                    yield return source;
                }
            }
        }
    }

    // The script a file of the project is the source of, when it is a script in an authoring mount.
    private (AssetPath Identity, FileSystemPath File)? SourceOf(AssetPath? path)
        => path is not null && AssetFileTypes.Is(path.Name, AssetFileCategory.Script)
            && this.Project.AuthoringMounts.Any(mount => string.Equals(mount.Name, path.Mount, StringComparison.Ordinal) && !ProjectOutputFolders.Contains(mount))
            && this.Project.Find(path) is { File: var file }
                ? (AssetFileTypes.ToIdentity(path), file)
                : null;

    private async Task AnalyzeAsync(AssetPath identity, FileSystemPath file, bool force, CancellationToken cancellationToken)
    {
        var version = this.versions.AddOrUpdate(identity, 1, static (_, current) => current + 1);

        // A deleted script is forgotten; reading it only fails when it goes while it is read.
        if (!this.Project.FileSystem.File.Exists(file.FullPath))
        {
            this.Commit(identity, version, entry: null);
            return;
        }

        byte[] bytes;
        try
        {
            bytes = await this.Project.FileSystem.File.ReadAllBytesAsync(file.FullPath, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
        {
            this.Commit(identity, version, entry: null);
            return;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // The editor that saved the file may still hold it; its next change analyzes it again.
            this.LogScriptUnreadable(file, exception);
            return;
        }

        var hash = Hash(bytes);
        var previous = Volatile.Read(ref this.entries).GetValueOrDefault(identity);
        if (!force && previous is not null && string.Equals(previous.Hash, hash, StringComparison.Ordinal) && previous.Script.SourcePath == file)
        {
            return;
        }

        await this.analyzer.WaitUntilAvailableAsync(cancellationToken).ConfigureAwait(false);
        var sourceName = file.IsSameOrInside(this.Project.ProjectRoot) ? file.GetPathBelow(this.Project.ProjectRoot) : file.Name;
        var analysis = this.analyzer.Analyze(sourceName, bytes, this.definitions());
        if (analysis.IsUnavailable)
        {
            this.reportUnavailable(analysis.UnavailableReason ?? "The script analyzer could not run.");
            return;
        }

        var lastValid = previous?.LastValidParameters ?? [];
        var script = CreateScript(identity, file, analysis, lastValid);
        this.Commit(identity, version, new ScriptEntry(hash, script, script.ParametersOutOfDate ? lastValid : script.Parameters));
    }

    // Publishes an analysis, or the removal of a source when the entry is null, unless a newer analysis started.
    private void Commit(AssetPath identity, long version, ScriptEntry? entry)
    {
        lock (this.gate)
        {
            if (this.disposed || (this.versions.TryGetValue(identity, out var latest) && latest != version))
            {
                return;
            }

            var before = this.entries;
            Volatile.Write(ref this.entries, entry is null ? before.Remove(identity) : before.SetItem(identity, entry));
            if (!ReferenceEquals(before, this.entries))
            {
                this.publish(ScriptSnapshot.Create(this.entries), identity);
            }
        }
    }
}
