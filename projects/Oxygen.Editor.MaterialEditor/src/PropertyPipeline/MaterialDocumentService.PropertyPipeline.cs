// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Atomically validates and commits schema-driven material property edits.</summary>
public sealed partial class MaterialDocumentService
{
    /// <inheritdoc/>
    public Task<MaterialEditResult> EditPropertiesAsync(Guid documentId, PropertyEdit edit, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(edit);
        cancellationToken.ThrowIfCancellationRequested();
        if (edit.Count == 0)
        {
            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }

        lock (this.sync)
        {
            var document = this.GetDocument(documentId);
            if (this.ValidatePropertyEdit(document, edit) is { } rejected)
            {
                return Task.FromResult(rejected);
            }

            var state = new MaterialEditState(document.Source);
            PropertyApply.ApplyToTarget(state, edit, MaterialDescriptors.Catalog.ById);
            if (state.Source == document.Source)
            {
                return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
            }

            if (this.ValidateEditedSource(document, state.Source) is { } invalidSource)
            {
                return Task.FromResult(invalidSource);
            }

            this.documents[documentId] = document with
            {
                Source = state.Source,
                Asset = CreateAsset(document.MaterialUri, state.Source),
                IsDirty = true,
                Revision = document.Revision + 1,
                CookState = MaterialCookState.Stale,
            };
            this.LogPropertiesApplied(documentId, document.MaterialUri, edit.Count);
            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }
    }

    private MaterialEditResult? ValidatePropertyEdit(MaterialDocument document, PropertyEdit edit)
    {
        var descriptors = MaterialDescriptors.Catalog.ById;
        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                return this.RejectPropertyEdit(document, "PROPERTY_UNKNOWN", $"Unknown property id: {id.Qualified()}.");
            }

            var validation = descriptor.ValidateBoxed(value);
            if (!validation.IsValid)
            {
                return this.RejectPropertyEdit(document, validation.Code, validation.Message);
            }
        }

        return null;
    }

    private MaterialEditResult? ValidateEditedSource(MaterialDocument document, MaterialSource source)
    {
        var validator = this.GetSchemaValidator();
        if (validator is null)
        {
            return null;
        }

        var json = MaterialSourceProjection.ToEngineJson(source);
        var engine = validator.ValidateAgainstEngineSchema(json);
        var overlay = validator.ValidateAgainstMergedSchema(json);
        return engine.IsValid && overlay.IsValid
            ? null
            : this.RejectPropertyEdit(document, "MATERIAL_SCHEMA_REJECTED", string.Join("; ", engine.Errors.Concat(overlay.Errors)));
    }

    private MaterialEditResult RejectPropertyEdit(MaterialDocument document, string code, string message)
    {
        this.LogPropertyEditRejected(document.DocumentId, document.MaterialUri, code, message);
        var operationId = this.PublishMaterialFailure(
            MaterialOperationKinds.EditScalar,
            document,
            code,
            "Material property edit was rejected.",
            message,
            FailureDomain.MaterialAuthoring);
        return new MaterialEditResult(Succeeded: false, OperationId: operationId);
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "Rejected material property edit. DocumentId={DocumentId} MaterialUri={MaterialUri} Code={Code}: {Message}")]
    private partial void LogPropertyEditRejected(Guid documentId, Uri materialUri, string code, string message);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Applied material property edit. DocumentId={DocumentId} MaterialUri={MaterialUri} PropertyCount={PropertyCount}")]
    private partial void LogPropertiesApplied(Guid documentId, Uri materialUri, int propertyCount);
}
