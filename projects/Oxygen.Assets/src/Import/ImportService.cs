// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Cook;

namespace Oxygen.Assets.Import;

/// <summary>
/// Default implementation of <see cref="IImportService"/>.
/// </summary>
public sealed class ImportService : IImportService
{
    private readonly ImporterRegistry registry;
    private readonly Func<string, IImportFileAccess> fileAccessFactory;
    private readonly Func<IAssetIdentityPolicy> identityPolicyFactory;
    private readonly Func<IImportFileAccess, ImportInput, IAssetImporter, ImportOptions, IAssetIdentityPolicy>? perInputIdentityPolicyFactory;

    /// <summary>
    /// Initializes a new instance of the <see cref="ImportService"/> class.
    /// </summary>
    /// <param name="registry">The importer registry.</param>
    public ImportService(ImporterRegistry registry)
        : this(
            registry,
            projectRoot => new SystemIoImportFileAccess(projectRoot),
            static (files, input, importer, options) => new SidecarAssetIdentityPolicy(files, input, importer, options))
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ImportService"/> class.
    /// </summary>
    /// <param name="registry">The importer registry.</param>
    /// <param name="fileAccessFactory">Factory that creates file access bound to the project root.</param>
    /// <param name="identityPolicyFactory">Factory that creates an identity policy per input.</param>
    public ImportService(
        ImporterRegistry registry,
        Func<string, IImportFileAccess> fileAccessFactory,
        Func<IImportFileAccess, ImportInput, IAssetImporter, ImportOptions, IAssetIdentityPolicy> identityPolicyFactory)
        : this(registry, fileAccessFactory, static () => new DeterministicVirtualPathIdentityPolicy())
    {
        ArgumentNullException.ThrowIfNull(identityPolicyFactory);
        this.perInputIdentityPolicyFactory = identityPolicyFactory;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="ImportService"/> class.
    /// </summary>
    /// <param name="registry">The importer registry.</param>
    /// <param name="fileAccessFactory">Factory that creates file access bound to the project root.</param>
    /// <param name="identityPolicyFactory">Factory that creates an identity policy instance.</param>
    public ImportService(
        ImporterRegistry registry,
        Func<string, IImportFileAccess> fileAccessFactory,
        Func<IAssetIdentityPolicy> identityPolicyFactory)
    {
        ArgumentNullException.ThrowIfNull(registry);
        ArgumentNullException.ThrowIfNull(fileAccessFactory);
        ArgumentNullException.ThrowIfNull(identityPolicyFactory);

        this.registry = registry;
        this.fileAccessFactory = fileAccessFactory;
        this.identityPolicyFactory = identityPolicyFactory;
        this.perInputIdentityPolicyFactory = null;
    }

    /// <inheritdoc />
    public async Task<ImportResult> ImportAsync(ImportRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        var state = this.CreateState(request);

        try
        {
            await this.ProcessInputsAsync(state, cancellationToken).ConfigureAwait(false);
            await BuildAsync(state, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            // Best-effort import: convert unexpected pipeline failures into diagnostics when FailFast is false.
            state.HadFailure = true;
            state.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_PIPELINE_FAILED",
                message: ex.Message,
                sourcePath: null);

            if (state.Request.Options.FailFast)
            {
                throw;
            }
        }
        finally
        {
            // Keep cooked roots mountable even after partial failures by repairing file records.
            if (!cancellationToken.IsCancellationRequested && state.HadFailure)
            {
                try
                {
                    var mountPoints = state.Request.Inputs
                        .Select(static i => i.MountPoint)
                        .Where(static m => !string.IsNullOrWhiteSpace(m));

                    await LooseCookedBuildService
                        .RepairIndexFileRecordsAsync(state.Files, mountPoints!, cancellationToken)
                        .ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    // Respect cancellation.
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"[ImportService] Index repair failed: {ex.Message}");
                }
            }
        }

        return state.ToResult();
    }

    private static async Task BuildAsync(ImportState state, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(state);

        if (state.Imported.Count == 0)
        {
            System.Diagnostics.Debug.WriteLine("[ImportService] No assets imported, skipping index build.");
            return;
        }

        try
        {
            System.Diagnostics.Debug.WriteLine($"[ImportService] Building index for {state.Imported.Count} assets.");
            await LooseCookedBuildService
                .BuildIndexAsync(state.Files, state.Imported, cancellationToken)
                .ConfigureAwait(false);
            System.Diagnostics.Debug.WriteLine("[ImportService] Index build succeeded.");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[ImportService] Index build failed: {ex.Message}");
            state.HadFailure = true;
            state.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYBUILD_INDEX_FAILED",
                message: ex.Message,
                sourcePath: null);

            if (state.Request.Options.FailFast)
            {
                throw;
            }
        }
    }

    private static async ValueTask<ImportProbe> CreateProbeAsync(
        IImportFileAccess files,
        string sourcePath,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);

        var extension = Path.GetExtension(sourcePath);
        extension = string.IsNullOrEmpty(extension) ? string.Empty : extension;

        var headerBytes = await files.ReadHeaderAsync(sourcePath, maxBytes: 64, cancellationToken).ConfigureAwait(false);
        return new ImportProbe(SourcePath: sourcePath, Extension: extension, HeaderBytes: headerBytes);
    }

    private static void ReportProgress(ImportState state, string stage, string currentItem)
        => state.Request.Options.Progress?.Report(
            new ImportProgress(
                Stage: stage,
                CurrentItem: currentItem,
                Completed: state.Completed,
                Total: state.Total));

    private static void MergeUpToDateForBuild(ImportState state)
    {
        // If any input was actually imported, include the up-to-date assets too so the build step
        // can generate complete mount point indexes for the requested input set.
        if (state.AnyImported && state.UpToDateImported.Count > 0)
        {
            state.Imported.AddRange(state.UpToDateImported);
        }
    }

    private static async Task<bool> TrySkipUpToDateAsync(
        ImportState state,
        ImportInput input,
        IAssetIdentityPolicy identity,
        CancellationToken cancellationToken)
    {
        if (state.Request.Options.ReimportIfUnchanged || identity is not SidecarAssetIdentityPolicy sidecar)
        {
            return false;
        }

        var upToDateAssets = await sidecar.TryGetUpToDateImportedAssetsAsync(cancellationToken).ConfigureAwait(false);
        if (upToDateAssets is null)
        {
            return false;
        }

        state.Diagnostics.Add(
            ImportDiagnosticSeverity.Info,
            code: "OXYIMPORT_UP_TO_DATE",
            message: $"Skipped import; inputs unchanged for '{input.SourcePath}'.",
            sourcePath: input.SourcePath);

        state.UpToDateImported.AddRange(upToDateAssets);
        return true;
    }

    private static async Task<IReadOnlyList<ImportedAsset>> RunImporterAsync(
        ImportState state,
        ImportInput input,
        IAssetImporter importer,
        IAssetIdentityPolicy identity,
        CancellationToken cancellationToken)
    {
        var context = new ImportContext(
            Files: state.Files,
            Input: input,
            Identity: identity,
            Options: state.Request.Options,
            Diagnostics: state.Diagnostics);

        var assets = await importer.ImportAsync(context, cancellationToken).ConfigureAwait(false);

        if (identity is SidecarAssetIdentityPolicy sidecarAfter)
        {
            await sidecarAfter.RecordImportAsync(assets, cancellationToken).ConfigureAwait(false);
        }

        return assets;
    }

    private async Task<bool> ProcessSingleInputAsync(
        ImportState state,
        ImportInput input,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        ReportProgress(state, stage: "Probe", currentItem: input.SourcePath);

        var probe = await CreateProbeAsync(state.Files, input.SourcePath, cancellationToken).ConfigureAwait(false);
        var importer = this.registry.Select(probe);
        if (importer is null)
        {
            state.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_NO_IMPORTER",
                message: $"No importer registered for '{input.SourcePath}'.",
                sourcePath: input.SourcePath);

            state.Completed++;
            return !state.Request.Options.FailFast;
        }

        ReportProgress(state, stage: "Import", currentItem: input.SourcePath);

        var identity = this.CreateIdentity(state, input, importer);
        if (await TrySkipUpToDateAsync(state, input, identity, cancellationToken).ConfigureAwait(false))
        {
            state.Completed++;
            return true;
        }

        try
        {
            var assets = await RunImporterAsync(state, input, importer, identity, cancellationToken).ConfigureAwait(false);
            state.Imported.AddRange(assets);
            state.AnyImported = state.AnyImported || assets.Count > 0;
            state.Completed++;
            return true;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            state.HadFailure = true;
            state.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_INPUT_FAILED",
                message: ex.Message,
                sourcePath: input.SourcePath);

            state.Completed++;
            return !state.Request.Options.FailFast;
        }
    }

    private ImportState CreateState(ImportRequest request)
    {
        var diagnostics = new ImportDiagnostics();
        var imported = new List<ImportedAsset>();
        var files = this.fileAccessFactory(request.ProjectRoot);
        var identity = this.identityPolicyFactory();

        return new ImportState(request, files, identity, diagnostics, imported);
    }

    private async Task ProcessInputsAsync(ImportState state, CancellationToken cancellationToken)
    {
        foreach (var input in state.Request.Inputs)
        {
            var shouldContinue = await this.ProcessSingleInputAsync(state, input, cancellationToken).ConfigureAwait(false);
            if (!shouldContinue)
            {
                break;
            }
        }

        MergeUpToDateForBuild(state);
    }

    private IAssetIdentityPolicy CreateIdentity(ImportState state, ImportInput input, IAssetImporter importer)
        => this.perInputIdentityPolicyFactory is null
            ? state.Identity
            : this.perInputIdentityPolicyFactory(state.Files, input, importer, state.Request.Options);

    private sealed class ImportState
    {
        public ImportState(
            ImportRequest request,
            IImportFileAccess files,
            IAssetIdentityPolicy identity,
            ImportDiagnostics diagnostics,
            List<ImportedAsset> imported)
        {
            ArgumentNullException.ThrowIfNull(request);
            ArgumentNullException.ThrowIfNull(files);
            ArgumentNullException.ThrowIfNull(identity);
            ArgumentNullException.ThrowIfNull(diagnostics);
            ArgumentNullException.ThrowIfNull(imported);

            this.Request = request;
            this.Files = files;
            this.Identity = identity;
            this.Diagnostics = diagnostics;
            this.Imported = imported;
            this.UpToDateImported = [];
            this.Total = request.Inputs.Count;
        }

        public ImportRequest Request { get; }

        public IImportFileAccess Files { get; }

        public IAssetIdentityPolicy Identity { get; }

        public ImportDiagnostics Diagnostics { get; }

        public List<ImportedAsset> Imported { get; }

        public List<ImportedAsset> UpToDateImported { get; }

        public bool AnyImported { get; set; }

        public bool HadFailure { get; set; }

        public int Total { get; }

        public int Completed { get; set; }

        public ImportResult ToResult()
        {
            var snapshot = this.Diagnostics.ToList();
            var succeeded = snapshot.All(d => d.Severity != ImportDiagnosticSeverity.Error);

            return new ImportResult(
                Imported: this.Imported,
                Diagnostics: snapshot,
                Succeeded: succeeded);
        }
    }
}
