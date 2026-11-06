// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The reference check of the session's project (source mounts, relocation across projects). A shared asset moved or
/// deleted while the project was closed leaves its references as written: this check lists each of them, with the
/// file, the field and the code, in the Problems pane and in <c>.build/references.json</c>, so a person or a tool can
/// rewrite them from the library's history. It writes nothing else.
/// </summary>
/// <remarks>
/// <para>
/// A reference into a source mount resolves when the mount is bound and holds the asset's source, or the source of the
/// container that produces it; one into a cooked mount, when the library is valid and holds the asset. Shared content
/// cannot reference a project's own mount, since no other project has it. A reference the grammar refuses names nothing,
/// and is listed with its refusal. References to the engine's content resolve through its catalog, not here.
/// </para>
/// <para>
/// The session activates the check: it runs when the project opens and after each change of the project's mounts, one
/// check at a time, and a user runs it on demand.
/// </para>
/// </remarks>
public sealed partial class ReferenceCheck : IReferenceCheck, IAssetProblemSource, IDisposable
{
    /// <summary>The name of the file the check writes in the project's <c>.build</c> folder.</summary>
    public const string FileName = "references.json";

    // The field of a model import record that names its source.
    private const string SourceProperty = "$.source";

    /// <summary>The version of the file tools read.</summary>
    internal const int ReferencesFileVersion = 3;

    /// <summary>The code of a reference into a library that does not hold the asset.</summary>
    internal const string ReferenceUnresolved = "reference.unresolved";

    /// <summary>The code of a reference from a shared mount into a project's own mount.</summary>
    internal const string ReferenceIntoProjectMount = "reference.into_project_mount";

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private readonly IProjectSession session;
    private readonly ILogger logger;
    private readonly SubscriberEvent<EventHandler> problemsChanged = new();
    private readonly SemaphoreSlim running = new(1, 1);
    private ImmutableArray<UnresolvedReference> unresolved = [];
    private ImmutableArray<AssetProblem> problems = [];

    /// <summary>Initializes a new instance of the <see cref="ReferenceCheck"/> class.</summary>
    /// <param name="session">The project session, whose project is checked.</param>
    /// <param name="logger">The logger.</param>
    public ReferenceCheck(IProjectSession session, ILogger<ReferenceCheck> logger)
    {
        ArgumentNullException.ThrowIfNull(session);
        ArgumentNullException.ThrowIfNull(logger);
        this.session = session;
        this.logger = logger;
        session.ContextChanged += this.OnContextChanged;
        this.RunInBackground();
    }

    /// <inheritdoc/>
    public event EventHandler? ProblemsChanged
    {
        add => this.problemsChanged.Add(value);
        remove => this.problemsChanged.Remove(value);
    }

    /// <inheritdoc/>
    public IReadOnlyList<UnresolvedReference> Unresolved => this.unresolved;

    /// <inheritdoc/>
    public IReadOnlyList<AssetProblem> Problems => this.problems;

    /// <inheritdoc/>
    public async Task<IReadOnlyList<UnresolvedReference>> RunAsync(CancellationToken cancellationToken)
    {
        await this.running.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var project = this.session.Context;
            var found = await FindAsync(project, cancellationToken).ConfigureAwait(false);
            var file = ProjectOutputFolders.BuildPath(project.ProjectRoot.FullPath, FileName);
            await WriteAsync(project, file, found, cancellationToken).ConfigureAwait(false);
            this.LogChecked(project.ProjectRoot, found.Length, file);
            _ = ImmutableInterlocked.InterlockedExchange(ref this.unresolved, found);
            _ = ImmutableInterlocked.InterlockedExchange(ref this.problems, [.. found.Select(static reference => new UnresolvedReferenceProblem(reference))]);
            this.problemsChanged.Invoke(handler => handler(this, EventArgs.Empty));
            return found;
        }
        finally
        {
            _ = this.running.Release();
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.session.ContextChanged -= this.OnContextChanged;
        this.running.Dispose();
    }

    /// <summary>Finds every unresolved reference of a project's authored files and model import records.</summary>
    /// <param name="project">The project.</param>
    /// <param name="cancellationToken">Cancels the scan.</param>
    /// <returns>The unresolved references, by file and field.</returns>
    internal static async Task<ImmutableArray<UnresolvedReference>> FindAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        var index = await AuthoredReferenceIndex.BuildAsync(project, cancellationToken).ConfigureAwait(false);
        var found = ImmutableArray.CreateBuilder<UnresolvedReference>();
        foreach (var descriptor in index.Descriptors)
        {
            found.AddRange(descriptor.Fields
                .Select(field => (Field: field, Code: index.BufferProducts.Contains(field.Identity) ? null : UnresolvedCode(project, descriptor.Identity, field.Identity)))
                .Where(static check => check.Code is not null)
                .Select(check => new UnresolvedReference(descriptor.Location, check.Field.Property, check.Field.Written, check.Field.Identity.VirtualPath, check.Code!)));
            found.AddRange(descriptor.Refused.Select(refused => new UnresolvedReference(descriptor.Location, refused.Property, refused.Written, Resolved: null, refused.Code)));
        }

        var records = new ModelImportRecords(project);
        found.AddRange(records.All.Select(record => (Record: record, Code: RecordSourceCode(project, record)))
            .Where(static check => check.Code is not null)
            .Select(static check => new UnresolvedReference(FileSystemPath.From(check.Record.RecordPath), SourceProperty, check.Record.Settings.Source, ResolvedSource(check.Record), check.Code!)));
        found.AddRange(records.Problems
            .Where(static problem => problem.UnboundSource is not null)
            .Select(problem => new UnresolvedReference(
                FileSystemPath.From(problem.RecordPath),
                SourceProperty,
                problem.UnboundSource!,
                problem.UnboundSource!,
                UnavailableMountCode(project, MountNameOf(problem.UnboundSource!)) ?? AssetIdentityDiagnosticCodes.ResolveMissing)));
        found.AddRange(records.Problems
            .Where(static problem => problem.RefusedSource is not null)
            .Select(static problem => new UnresolvedReference(FileSystemPath.From(problem.RecordPath), SourceProperty, problem.RefusedSource!, Resolved: null, problem.RefusalCode!)));
        return [.. found.OrderBy(static reference => reference.File.FullPath, StringComparer.Ordinal)
            .ThenBy(static reference => reference.Property, StringComparer.Ordinal)];
    }

    /// <summary>Gets why a reference does not resolve in the project.</summary>
    /// <param name="project">The project.</param>
    /// <param name="referrer">The identity of the file that holds the reference.</param>
    /// <param name="identity">The referenced identity.</param>
    /// <returns>The code, or <see langword="null"/> when it resolves.</returns>
    internal static string? UnresolvedCode(ProjectContext project, AssetPath referrer, AssetPath identity)
        => IsIntoProjectMount(project, referrer, identity) ? ReferenceIntoProjectMount : UnresolvedCode(project, identity);

    /// <summary>Gets why a referenced identity does not resolve in the project's mounts.</summary>
    /// <param name="project">The project.</param>
    /// <param name="identity">The referenced identity.</param>
    /// <returns>The code, or <see langword="null"/> when it resolves.</returns>
    internal static string? UnresolvedCode(ProjectContext project, AssetPath identity)
    {
        var mount = project.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, identity.Mount, StringComparison.Ordinal));
        if (mount is null)
        {
            // The engine's content and the project's own files are always mounted; any other unknown mount is gone.
            return string.Equals(identity.Mount, AssetPath.Engine.Mount, StringComparison.Ordinal)
                || string.Equals(identity.Mount, ProjectAssetFiles.ProjectFilesMount, StringComparison.Ordinal)
                    ? null : AssetIdentityDiagnosticCodes.ResolveMissing;
        }

        return mount.Kind == MountKind.Cooked ? UnresolvedLibraryCode(mount, identity)
            : UnavailableMountCode(project, mount.Name) is { } unavailable ? unavailable
            : CookInputResolver.HasProjectSource(project, identity)
                || (AssetFileTypes.ContainerOf(identity) is { } container && CookInputResolver.HasProjectSource(project, container))
                ? null : AssetIdentityDiagnosticCodes.ResolveMissing;
    }

    // A library that is not usable resolves nothing; a usable one resolves what it holds.
    private static string? UnresolvedLibraryCode(ResolvedMount mount, AssetPath identity)
        => mount.State switch
        {
            MountState.Unbound => MountCodes.Unbound,
            MountState.Missing => MountCodes.FolderMissing,
            MountState.Bound when mount.Library?.Holds(identity.VirtualPath) == true => null,
            MountState.Bound => ReferenceUnresolved,
            _ => mount.Findings.Count != 0 ? mount.Findings[0].Code : ReferenceUnresolved,
        };

    // Shared content is read by other projects, which do not have this project's own mounts.
    private static bool IsIntoProjectMount(ProjectContext project, AssetPath referrer, AssetPath identity)
        => !string.Equals(referrer.Mount, identity.Mount, StringComparison.Ordinal)
            && project.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, referrer.Mount, StringComparison.Ordinal)) is { Kind: MountKind.Source, Role: MountRole.Shared }
            && project.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, identity.Mount, StringComparison.Ordinal)) is { Kind: MountKind.Source, Role: MountRole.Own };

    // A model's record names its source; a model moved or deleted while the project was closed leaves it unresolved.
    private static string? RecordSourceCode(ProjectContext project, ModelImportRecord record)
        => AssetPath.TryParse(ResolvedSource(record), out var source) && IsIntoProjectMount(project, record.Identity, source) ? ReferenceIntoProjectMount
            : project.FileSystem.File.Exists(record.SourcePath) ? null
            : AssetIdentityDiagnosticCodes.ResolveMissing;

    private static string ResolvedSource(ModelImportRecord record)
        => AssetPath.TryResolveReference(record.Identity, record.Settings.Source, VirtualPathRuleSet.Cooked, out var source, out _) ? source.VirtualPath : record.Settings.Source;

    // Why a declared source mount cannot be read on this machine, or null when it can or is not a source mount.
    private static string? UnavailableMountCode(ProjectContext project, string name)
        => project.Mounts.FirstOrDefault(mount => mount.Kind == MountKind.Source && string.Equals(mount.Name, name, StringComparison.Ordinal)) switch
        {
            { State: MountState.Unbound } => MountCodes.Unbound,
            { State: MountState.Missing } => MountCodes.FolderMissing,
            _ => null,
        };

    private static string MountNameOf(string mountPath)
    {
        var end = mountPath.IndexOf('/', 1);
        return end < 0 ? mountPath[1..] : mountPath[1..end];
    }

    // Writes the list beside the project's other build output, replacing the previous list in one move.
    private static async Task WriteAsync(ProjectContext project, string path, ImmutableArray<UnresolvedReference> found, CancellationToken cancellationToken)
    {
        var document = new ReferencesFile(ReferencesFileVersion, [.. found.Select(reference => new ReferencesFile.Entry(
            CookPaths.TryOf(project, reference.File) ?? reference.File.FullPath,
            reference.Property,
            reference.Written,
            reference.Resolved,
            reference.Code))]);
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        var stream = project.FileSystem.File.Create(temporary);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, document, JsonOptions, cancellationToken).ConfigureAwait(false);
        }

        project.FileSystem.File.Move(temporary, path, overwrite: true);
    }

    private void OnContextChanged(object? sender, EventArgs args) => this.RunInBackground();

    // A check the session starts ends with the session; one that cannot read or write the project's files reports why.
    private void RunInBackground()
        => _ = Task.Run(
            async () =>
            {
                try
                {
                    _ = await this.RunAsync(this.session.Lifetime).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (this.session.Lifetime.IsCancellationRequested)
                {
                    // The session closed.
                }
                catch (ObjectDisposedException) when (this.session.Lifetime.IsCancellationRequested)
                {
                    // The session closed between two checks.
                }
                catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
                {
                    this.LogCheckFailed(failure);
                }
            },
            CancellationToken.None);

    // The file tools read: each unresolved reference by the cook path of its file, its field, what it writes, what that
    // resolves to, and the code.
    private sealed record ReferencesFile(
        [property: JsonPropertyName("schema_version")] int Version,
        [property: JsonPropertyName("references")] IReadOnlyList<ReferencesFile.Entry> References)
    {
        /// <summary>One unresolved reference.</summary>
        /// <param name="File">The referencing file's cook path.</param>
        /// <param name="Property">The field's JSON path.</param>
        /// <param name="Written">The reference as the file writes it.</param>
        /// <param name="Resolved">The virtual path it resolves to, absent when the grammar refuses it.</param>
        /// <param name="Code">Why it does not resolve.</param>
        public sealed record Entry(
            [property: JsonPropertyName("file")] string File,
            [property: JsonPropertyName("property")] string Property,
            [property: JsonPropertyName("written")] string Written,
            [property: JsonPropertyName("resolved"), JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Resolved,
            [property: JsonPropertyName("code")] string Code);
    }
}
