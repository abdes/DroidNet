// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Projects;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
public partial class ProjectManagerService
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene `{scene}` is open but its file `{location}` was deleted or moved outside the editor; saving writes it again.")]
    partial void SceneSourceMissing(string scene, string location);

    [LoggerMessage(Level = LogLevel.Information, Message = "Scene `{scene}` file is back on disk (unchanged: {unchanged}).")]
    partial void SceneSourceRestored(string scene, bool unchanged);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotSaveProjectInfo(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProjectInfo(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProject(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load scene from `{location}`; {error}")]
    partial void CouldNotLoadScene(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load scene from `{location}`; {error}")]
    partial void CouldNotLoadSceneEntities(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not create scene `{sceneName}`; {error}")]
    partial void CouldNotCreateScene(string sceneName, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not save scene `{sceneName}`; {error}")]
    partial void CouldNotSaveScene(string sceneName, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Failed to load scene metadata from {ScenePath}")]
    partial void CouldNotLoadSceneMetadata(Exception ex, string ScenePath);
}
