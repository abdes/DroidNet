// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Connects a scene document's saved identity and UI-owned read state to cook capture.</summary>
/// <param name="documents">The shared registry of saved authoring inputs.</param>
/// <param name="projectManager">The owner of acknowledged scene source versions.</param>
/// <param name="hostingContext">The authoring UI dispatcher.</param>
public sealed class SceneCookInputRegistrar(
    ICookDocumentRegistry documents,
    IProjectManagerService projectManager,
    HostingContext hostingContext)
{
    /// <summary>Registers a loaded scene until its document or saved source changes.</summary>
    /// <param name="context">The scene document's current authoring ownership.</param>
    /// <param name="commands">The owner of the document's save gate and gesture state.</param>
    /// <returns>A registration to dispose, or null until the scene has a saved source.</returns>
    public IDisposable? Register(SceneDocumentCommandContext context, ISceneDocumentCommandService commands)
        => projectManager.GetSceneSourceVersion(context.Scene) is { } source
            ? documents.Register(source.SourcePath, token => hostingContext.Dispatcher.DispatchAsync(() => commands.AcquireCookReadAsync(context, token)))
            : null;
}
