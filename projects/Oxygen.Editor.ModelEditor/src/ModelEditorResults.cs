// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>The operation results the Model Editor publishes: what a reimport of its model, or a save of its import
/// settings, did.</summary>
internal static class ModelEditorResults
{
    /// <summary>The title of a reimport's result.</summary>
    public const string ReimportTitle = "Reimport model";

    /// <summary>The title of the result of a save, which writes the import settings and reimports.</summary>
    public const string SaveTitle = "Save import settings";

    /// <summary>Gets the scope a result about a model names: the model's source.</summary>
    /// <param name="source">The model's source file.</param>
    /// <returns>The scope.</returns>
    public static AffectedScope ScopeOf(AssetPath source)
        => new() { AssetId = source.ToUriString(), AssetVirtualPath = source.VirtualPath };

    /// <summary>Publishes what a reimport did, with the diagnostics it reported.</summary>
    /// <param name="results">The operation-result publisher.</param>
    /// <param name="reducer">Reduces the diagnostics into a severity.</param>
    /// <param name="title">The result's title, <see cref="ReimportTitle"/> or <see cref="SaveTitle"/>.</param>
    /// <param name="result">The reimport's result.</param>
    /// <param name="source">The model's source file.</param>
    public static void PublishReimport(IOperationResultPublisher results, IStatusReducer reducer, string title, ContentCookResult result, AssetPath source)
    {
        var diagnostics = result.ReportedDiagnostics;
        var status = result.ReportedStatus;
        results.Publish(new OperationResult
        {
            OperationId = result.OperationId,
            OperationKind = ContentPipelineOperationKinds.Import,
            Status = status,
            Severity = status == OperationStatus.Failed && diagnostics.Count == 0
                ? DiagnosticSeverity.Error
                : reducer.ComputeSeverity(diagnostics),
            Title = title,
            Message = status == OperationStatus.Failed ? $"{source.Name} was not reimported." : $"{source.Name} was reimported.",
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = ScopeOf(source),
            Diagnostics = diagnostics,
        });
    }

    /// <summary>Publishes a reimport that failed before the content pipeline reported a result.</summary>
    /// <param name="results">The operation-result publisher.</param>
    /// <param name="title">The result's title, <see cref="ReimportTitle"/> or <see cref="SaveTitle"/>.</param>
    /// <param name="source">The model's source file.</param>
    /// <param name="exception">What failed.</param>
    public static void PublishReimportFailure(IOperationResultPublisher results, string title, AssetPath source, Exception exception)
    {
        var operationId = Guid.NewGuid();
        var message = $"{source.Name} was not reimported.";
        results.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = ContentPipelineOperationKinds.Import,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = ScopeOf(source),
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = AssetImportDiagnosticCodes.ImportFailed,
                    Message = message,
                    TechnicalMessage = exception.Message,
                    ExceptionType = exception.GetType().FullName,
                },
            ],
        });
    }
}
