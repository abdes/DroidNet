// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
        await this.ProcessInputsAsync(state, cancellationToken).ConfigureAwait(false);
        return state.ToResult();
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
            cancellationToken.ThrowIfCancellationRequested();

            state.Request.Options.Progress?.Report(
                new ImportProgress(
                    Stage: "Probe",
                    CurrentItem: input.SourcePath,
                    Completed: state.Completed,
                    Total: state.Total));

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

                if (state.Request.Options.FailFast)
                {
                    return;
                }

                continue;
            }

            state.Request.Options.Progress?.Report(
                new ImportProgress(
                    Stage: "Import",
                    CurrentItem: input.SourcePath,
                    Completed: state.Completed,
                    Total: state.Total));

            var context = new ImportContext(
                Files: state.Files,
                Input: input,
                Identity: this.CreateIdentity(state, input, importer),
                Options: state.Request.Options,
                Diagnostics: state.Diagnostics);

            var assets = await importer.ImportAsync(context, cancellationToken).ConfigureAwait(false);
            state.Imported.AddRange(assets);
            state.Completed++;
        }
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
            this.Total = request.Inputs.Count;
        }

        public ImportRequest Request { get; }

        public IImportFileAccess Files { get; }

        public IAssetIdentityPolicy Identity { get; }

        public ImportDiagnostics Diagnostics { get; }

        public List<ImportedAsset> Imported { get; }

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
