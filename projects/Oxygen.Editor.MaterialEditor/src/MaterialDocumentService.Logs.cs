// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Source-generated diagnostics for material authoring and persistence.</summary>
public sealed partial class MaterialDocumentService
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Rejected material scalar edit. DocumentId={DocumentId} MaterialUri={MaterialUri} Field={FieldKey} ValueType={ValueType} Value={Value}")]
    private partial void LogScalarEditRejected(Guid documentId, Uri materialUri, string fieldKey, string valueType, object? value);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Rejected material save because engine schema validation failed. DocumentId={DocumentId} MaterialUri={MaterialUri} Errors={Errors}")]
    private partial void LogSaveSchemaRejected(Guid documentId, Uri materialUri, string errors);

    [LoggerMessage(Level = LogLevel.Information, Message = "Rejected material cook because descriptor is dirty. DocumentId={DocumentId} MaterialUri={MaterialUri} SourcePath={SourcePath}")]
    private partial void LogCookDirty(Guid documentId, Uri materialUri, string sourcePath);

    [LoggerMessage(Level = LogLevel.Information, Message = "Material cook completed. DocumentId={DocumentId} MaterialUri={MaterialUri} State={State} OperationId={OperationId}")]
    private partial void LogCookCompleted(Guid documentId, Uri materialUri, MaterialCookState state, Guid? operationId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Loaded material schema validator from assembly output.")]
    private partial void LogSchemaLoaded();

    [LoggerMessage(Level = LogLevel.Warning, Message = "Material schema validator could not be loaded; material saves will rely on cooker validation.")]
    private partial void LogSchemaUnavailable(Exception exception);
}
