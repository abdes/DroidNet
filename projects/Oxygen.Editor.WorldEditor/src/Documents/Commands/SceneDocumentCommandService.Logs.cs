// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
public sealed partial class SceneDocumentCommandService
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Scene '{Scene}' followed a relocation in memory: {Nodes} nodes re-pointed, environment changed: {Environment}.")]
    private partial void LogSceneFollowedRelocation(string scene, int nodes, bool environment);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene '{Scene}' is loading or busy with another edit and could not follow the relocation; its next save reports the file conflict.")]
    private partial void LogSceneCouldNotFollowRelocation(string scene);

    [LoggerMessage(Level = LogLevel.Information, Message = "Scene '{Scene}': refreshed {Nodes} relocated nodes in the runtime after the cook published (environment: {Environment}).")]
    private partial void LogRuntimeRefreshedAfterRelocation(string scene, int nodes, bool environment);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene '{Scene}': {Nodes} relocated nodes are not refreshed in the runtime because the cook did not publish: {Problem}")]
    private partial void LogRuntimeRefreshSkipped(string scene, int nodes, string problem);

    [LoggerMessage(Level = LogLevel.Error, Message = "Scene '{Scene}': could not refresh relocated nodes in the runtime.")]
    private partial void LogRuntimeRefreshFailed(Exception exception, string scene);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene '{Scene}' restored a reference to the deleted asset(s) {Assets}; it shows as missing.")]
    private partial void LogRestoredDeletedReference(string scene, string assets);
}
