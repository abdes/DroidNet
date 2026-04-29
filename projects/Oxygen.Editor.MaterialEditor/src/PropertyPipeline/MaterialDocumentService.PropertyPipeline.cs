// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Schema-driven property pipeline implementation for the material
/// document service.
/// </summary>
public sealed partial class MaterialDocumentService
{
    /// <inheritdoc />
    public Task<MaterialEditResult> EditPropertiesAsync(
        Guid documentId,
        PropertyEdit edit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(edit);
        cancellationToken.ThrowIfCancellationRequested();

        if (edit.Count == 0)
        {
            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }

        MaterialDocument document;
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out document!))
            {
                throw new KeyNotFoundException($"Material document '{documentId}' is not open.");
            }
        }

        // 1. Validate every entry against the descriptor catalog.
        var descriptors = MaterialDescriptors.Catalog.ById;
        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                this.logger.LogWarning(
                    "Rejected material property edit with unknown property id. DocumentId={DocumentId} MaterialUri={MaterialUri} PropertyId={PropertyId}",
                    document.DocumentId,
                    document.MaterialUri,
                    id.Qualified());
                var operationId = this.PublishMaterialFailure(
                    MaterialOperationKinds.EditScalar,
                    document,
                    "PROPERTY_UNKNOWN",
                    "Material property edit was rejected.",
                    $"Unknown property id: {id.Qualified()}.",
                    FailureDomain.MaterialAuthoring);
                return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: operationId));
            }

            var validation = descriptor.ValidateBoxed(value);
            if (!validation.IsValid)
            {
                this.logger.LogWarning(
                    "Rejected material property edit. DocumentId={DocumentId} MaterialUri={MaterialUri} PropertyId={PropertyId} Code={Code} ValueType={ValueType} Value={Value}",
                    document.DocumentId,
                    document.MaterialUri,
                    id.Qualified(),
                    validation.Code,
                    value?.GetType().FullName ?? "<null>",
                    value);
                var operationId = this.PublishMaterialFailure(
                    MaterialOperationKinds.EditScalar,
                    document,
                    validation.Code,
                    "Material property edit was rejected.",
                    validation.Message,
                    FailureDomain.MaterialAuthoring);
                return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: operationId));
            }
        }

        // 2. Apply via the schema-layer pure function on the mutable
        //    adapter. The descriptor writers translate engine-pointer
        //    ids to glTF-style record updates on MaterialSource.
        var state = new MaterialEditState(document.Source);
        PropertyApply.ApplyToTarget(state, edit, descriptors);
        var updatedSource = state.Source;

        if (updatedSource == document.Source)
        {
            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }

        var validator = this.GetSchemaValidator();
        if (validator is not null)
        {
            var engineJson = MaterialSourceProjection.ToEngineJson(updatedSource);
            var engineValidation = validator.ValidateAgainstEngineSchema(engineJson);
            var overlayValidation = validator.ValidateAgainstMergedSchema(engineJson);
            if (!engineValidation.IsValid || !overlayValidation.IsValid)
            {
                this.logger.LogWarning(
                    "Rejected material property edit because schema validation failed. DocumentId={DocumentId} MaterialUri={MaterialUri} EngineValid={EngineValid} OverlayValid={OverlayValid} Errors={Errors}",
                    document.DocumentId,
                    document.MaterialUri,
                    engineValidation.IsValid,
                    overlayValidation.IsValid,
                    string.Join("; ", engineValidation.Errors.Concat(overlayValidation.Errors)));
                var operationId = this.PublishMaterialFailure(
                    MaterialOperationKinds.EditScalar,
                    document,
                    "MATERIAL_SCHEMA_REJECTED",
                    "Material property edit was rejected.",
                    string.Join("; ", engineValidation.Errors.Concat(overlayValidation.Errors)),
                    FailureDomain.MaterialAuthoring);
                return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: operationId));
            }
        }

        // 3. Commit. Same lock pattern as EditScalarAsync.
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out var current))
            {
                throw new KeyNotFoundException($"Material document '{documentId}' is not open.");
            }

            this.documents[documentId] = current with
            {
                Source = updatedSource,
                Asset = CreateAsset(current.MaterialUri, updatedSource),
                IsDirty = true,
                CookState = MaterialCookState.Stale,
            };
        }

        this.logger.LogDebug(
            "Applied material property edit. DocumentId={DocumentId} MaterialUri={MaterialUri} PropertyCount={PropertyCount}",
            documentId,
            document.MaterialUri,
            edit.Count);
        return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
    }
}
