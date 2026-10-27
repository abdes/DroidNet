// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The model imports the Model Editor runs: a reimport, and the save of a model's import settings, which the content
/// pipeline, the one writer of model import records, writes and imports as one operation (model editor ME-6).
/// </summary>
/// <remarks>
/// Each publishes what it did in the operation results; the project's assets, and so the model's import status, follow
/// the cook run as any import's do. A save reaches here from any save of the model's document: Ctrl+S, the close
/// prompt or Save all, so this depends on nothing that follows the open documents.
/// </remarks>
/// <param name="pipeline">The content pipeline.</param>
/// <param name="session">The project session.</param>
/// <param name="results">Publishes what an import did.</param>
/// <param name="reducer">Reduces an import's diagnostics into a severity.</param>
/// <param name="loggerFactory">The logger factory.</param>
public sealed partial class ModelImports(
    IContentPipelineService pipeline,
    IProjectSession session,
    IOperationResultPublisher results,
    IStatusReducer reducer,
    ILoggerFactory loggerFactory) : IModelImportRecordWriter
{
    private readonly ILogger logger = loggerFactory.CreateLogger<ModelImports>();
    private readonly HashSet<AssetPath> importing = [];

    /// <summary>Occurs when a model's import starts or ends.</summary>
    public event EventHandler? ImportingChanged;

    /// <summary>Gets whether an import of a model, started here, runs.</summary>
    /// <param name="source">The model's source file.</param>
    /// <returns><see langword="true"/> while it runs.</returns>
    public bool IsImporting(AssetPath source) => this.importing.Contains(source);

    /// <summary>Reimports a model with its import settings.</summary>
    /// <param name="source">The model's source file.</param>
    /// <returns>A task that completes when the reimport did, or failed.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "This user-command boundary reports the failure; the content pipeline owns the import and its recovery.")]
    public async Task ReimportAsync(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        var project = session.Context;
        this.LogReimportRequested(source);
        this.Begin(source);
        try
        {
            var result = await pipeline.ReimportSourceAsync(source, project, CancellationToken.None).ConfigureAwait(true);
            ModelEditorResults.PublishReimport(results, reducer, ModelEditorResults.ReimportTitle, result, source);
        }
        catch (OperationCanceledException)
        {
            // The Cooking run keeps the cancellation.
        }
        catch (Exception ex)
        {
            this.LogImportFailed(ex, source);
            ModelEditorResults.PublishReimportFailure(results, ModelEditorResults.ReimportTitle, source, ex);
        }
        finally
        {
            this.End(source);
        }
    }

    /// <inheritdoc/>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A save reports why it failed to the document; the content pipeline owns the import and its recovery.")]
    public async Task<DocumentSaveResult> ReplaceAsync(AssetPath model, ReadOnlyMemory<byte> expected, ModelImportSettings replacement, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(replacement);
        var project = session.Context;
        this.LogSaveRequested(model);
        this.Begin(model);
        try
        {
            var written = replacement.ToBytes();
            var result = await pipeline.ReplaceModelRecordAsync(model, expected, written, project, cancellationToken).ConfigureAwait(true);
            ModelEditorResults.PublishReimport(results, reducer, ModelEditorResults.SaveTitle, result, model);
            return OutcomeOf(project, model, expected, written, result);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            this.LogImportFailed(ex, model);
            ModelEditorResults.PublishReimportFailure(results, ModelEditorResults.SaveTitle, model, ex);
            return new DocumentSaveResult(DocumentSaveOutcome.Refused, ex.Message);
        }
        finally
        {
            this.End(model);
        }
    }

    // The save is what the record's file holds after the operation: the replacement, even when the reimport then failed,
    // which the model's import status reports; the bytes as they were read, when the write was refused; else another
    // change.
    private static DocumentSaveResult OutcomeOf(ProjectContext project, AssetPath model, ReadOnlyMemory<byte> expected, byte[] replacement, ContentCookResult result)
    {
        var record = ModelRecordLookup.TryFind(project, model) is { } found ? project.FileSystem.File.ReadAllBytes(found.RecordPath) : null;
        if (record is not null && record.AsSpan().SequenceEqual(replacement))
        {
            return new DocumentSaveResult(DocumentSaveOutcome.Saved);
        }

        if (record is not null && !record.AsSpan().SequenceEqual(expected.Span))
        {
            return new DocumentSaveResult(DocumentSaveOutcome.Conflict, $"The import settings of {model.Name} changed since the Model Editor read them; they were not saved.");
        }

        var reason = result.Diagnostics.FirstOrDefault(static issue => issue.Severity >= DiagnosticSeverity.Error)?.Message;
        return new DocumentSaveResult(DocumentSaveOutcome.Refused, reason ?? $"The import settings of {model.Name} were not saved.");
    }

    private void Begin(AssetPath source)
    {
        _ = this.importing.Add(source);
        this.ImportingChanged?.Invoke(this, EventArgs.Empty);
    }

    private void End(AssetPath source)
    {
        _ = this.importing.Remove(source);
        this.ImportingChanged?.Invoke(this, EventArgs.Empty);
    }
}
