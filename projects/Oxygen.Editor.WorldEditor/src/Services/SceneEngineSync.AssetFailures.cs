// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Publishes current asynchronous asset failures through the existing operation surface.</summary>
public sealed partial class SceneEngineSync
{
    private IRuntimeWorldCommands? observedWorld;

    private void ObserveWorld(IRuntimeWorldCommands commands)
    {
        if (ReferenceEquals(commands, this.observedWorld))
        {
            return;
        }

        if (this.observedWorld is { } observedWorld)
        {
            observedWorld.AssetLoadFailed -= this.OnAssetLoadFailed;
        }

        this.observedWorld = commands;
        commands.AssetLoadFailed += this.OnAssetLoadFailed;
    }

    private void OnAssetLoadFailed(object? sender, RuntimeAssetLoadFailedEventArgs args)
    {
        if (sender is not IRuntimeWorldCommands commands || !ReferenceEquals(commands, this.observedWorld))
        {
            return;
        }

        if (hostingContext is { } context && !context.Dispatcher.HasThreadAccess)
        {
            if (!context.Dispatcher.TryEnqueue(() => this.PublishAssetLoadFailure(commands, args)))
            {
                this.LogAssetFailureDispatchUnavailable(args.Request.OperationId);
            }
        }
        else
        {
            this.PublishAssetLoadFailure(commands, args);
        }
    }

    private void PublishAssetLoadFailure(IRuntimeWorldCommands commands, RuntimeAssetLoadFailedEventArgs args)
    {
        var request = args.Request;
        if (!ReferenceEquals(commands, this.observedWorld) || this.activeWorld?.Target != request.Target
            || this.activeScene is not { } scene || !commands.IsCurrentAssetRequest(request))
        {
            return;
        }

        var (nodeId, assetPath, operationKind, code) = request.Command switch
        {
            RuntimeSetGeometry geometry => (geometry.NodeId, geometry.AssetPath, SceneOperationKinds.EditGeometry, LiveSyncDiagnosticCodes.GeometryUnresolvedAtRuntime),
            RuntimeSetMaterialOverride material => (material.NodeId, material.MaterialPath, SceneOperationKinds.EditMaterialSlot, LiveSyncDiagnosticCodes.MaterialFailed),
            _ => (Guid.Empty, null, string.Empty, string.Empty),
        };
        var node = FindNode(scene, nodeId);
        if (node is null)
        {
            return;
        }

        this.LogAssetLoadFailed(request.OperationId, args.Generation, nodeId, args.Message);
        if (this.GetAssetFailureScope(scene, node, assetPath, request.Target.DocumentLifetime) is not { } scope)
        {
            return;
        }

        operationResults?.Publish(new OperationResult
        {
            OperationId = request.OperationId,
            OperationKind = operationKind,
            Status = OperationStatus.PartiallySucceeded,
            Severity = DiagnosticSeverity.Error,
            Title = "Runtime asset load failed",
            Message = args.Message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = scope,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = request.OperationId,
                    Domain = FailureDomain.LiveSync,
                    Severity = DiagnosticSeverity.Error,
                    Code = code,
                    Message = args.Message,
                    TechnicalMessage = string.Create(CultureInfo.InvariantCulture, $"Native asset request generation {args.Generation}: {args.Message}"),
                    AffectedPath = assetPath,
                    AffectedEntity = scope,
                },
            ],
        });
    }

    private AffectedScope? GetAssetFailureScope(Scene scene, SceneNode node, string? assetPath, Guid documentLifetime)
    {
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime) && lifetime.Id == documentLifetime
                ? Scope(scene, node, componentType: nameof(GeometryComponent), assetVirtualPath: assetPath) with
                {
                    DocumentId = lifetime.Metadata.DocumentId,
                    DocumentLifetime = lifetime.Id,
                }
                : null;
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Runtime asset operation {OperationId}, native generation {Generation}, node {NodeId} failed: {Message}")]
    private partial void LogAssetLoadFailed(Guid operationId, ulong generation, Guid nodeId, string message);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot dispatch runtime asset failure {OperationId}: the UI dispatcher is unavailable.")]
    private partial void LogAssetFailureDispatchUnavailable(Guid operationId);
}
