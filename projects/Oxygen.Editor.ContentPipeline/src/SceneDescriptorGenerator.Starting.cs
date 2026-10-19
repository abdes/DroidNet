// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Projects how the scene starts when played: the starting camera and the starting input contexts.
/// </summary>
public sealed partial class SceneDescriptorGenerator
{
    /// <summary>Resolves the camera node the cooked scene starts viewing through.</summary>
    /// <remarks>
    /// An unset starting camera cooks as the first camera in hierarchy order, which is the camera the editor shows
    /// as Automatic; the runtime then has no fallback to report.
    /// </remarks>
    /// <param name="scene">The scene.</param>
    /// <param name="nodeIndices">Each node's index in the cooked scene.</param>
    /// <param name="site">Where a problem is reported.</param>
    /// <returns>The camera's node index; <see langword="null"/> when the scene has no camera or the authored one is
    /// missing, which is reported.</returns>
    private static int? CreateStartingCamera(Scene scene, IReadOnlyDictionary<NodeId, int> nodeIndices, PropertySite site)
    {
        if (scene.StartingCamera is { } authored && scene.EffectiveStartingCamera is null)
        {
            var name = scene.AllNodes.FirstOrDefault(node => node.Id == authored)?.Name;
            site.Report(
                SceneStartingPropertyPaths.StartingCamera,
                name is null
                    ? "The starting camera was deleted. Pick another camera in the scene's Play section, or return it to Automatic."
                    : $"The starting camera '{name}' is no longer a camera. Pick another camera in the scene's Play section, or return it to Automatic.");
            return null;
        }

        return scene.EffectiveStartingCamera is { } camera ? nodeIndices[camera.Id] : null;
    }

    /// <summary>Resolves the input mapping contexts the cooked scene activates when it starts.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="scope">The cook scope, whose project holds the input files.</param>
    /// <param name="site">Where a problem is reported.</param>
    /// <returns>The contexts with the priority each starts with, or <see langword="null"/> when there are none.</returns>
    private static List<NativeStartingInputContext>? CreateStartingInputContexts(Scene scene, ContentCookScope scope, PropertySite site)
    {
        var contexts = new List<NativeStartingInputContext>();
        foreach (var entry in scene.StartingInputContexts)
        {
            var path = entry.Context.VirtualPath;
            if (!path.EndsWith(AssetFileTypes.InputMappingContext, StringComparison.Ordinal))
            {
                site.Report(SceneStartingPropertyPaths.StartingInputContexts, $"'{path}' is not an input mapping context. Remove it from the scene's starting input contexts.");
                continue;
            }

            if (InputContextSources.Find(scope.Project, entry.Context).IsUndeclared)
            {
                site.Report(
                    SceneStartingPropertyPaths.StartingInputContexts,
                    $"No input source declares the starting input context '{path}'. Remove it, or declare it in the input source it is named under.");
                continue;
            }

            contexts.Add(new NativeStartingInputContext(
                ContentPipelinePaths.ToNativeDescriptorPath(entry.Context, AssetFileTypes.InputMappingContext),
                InputContextSources.EffectivePriority(scope.Project, entry)));
        }

        return contexts.Count == 0 ? null : contexts;
    }

    /// <summary>
    /// Reports scene-level property problems with Go To Property, which opens the scene and focuses the field.
    /// </summary>
    /// <param name="OperationId">The cook operation.</param>
    /// <param name="Scope">The cook scope.</param>
    /// <param name="SceneInput">The scene being cooked.</param>
    /// <param name="Scene">The scene.</param>
    /// <param name="Diagnostics">Receives the problems.</param>
    private sealed record PropertySite(Guid OperationId, ContentCookScope Scope, ContentCookInput SceneInput, Scene Scene, List<DiagnosticRecord> Diagnostics)
    {
        /// <summary>Reports an error on one scene property.</summary>
        /// <param name="propertyPath">The stable property path Go To Property focuses.</param>
        /// <param name="message">What is wrong and how to fix it.</param>
        /// <param name="technicalMessage">The offending value, when the message does not name it.</param>
        public void Report(string propertyPath, string message, string? technicalMessage = null)
            => this.Diagnostics.Add(new DiagnosticRecord
            {
                OperationId = this.OperationId,
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Error,
                Code = ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed,
                Message = message,
                TechnicalMessage = technicalMessage ?? $"Scene '{this.Scene.Name}': {propertyPath}.",
                AffectedPath = this.SceneInput.SourceAbsolutePath.FullPath,
                AffectedVirtualPath = this.SceneInput.AssetUri.VirtualPath,
                AffectedEntity = new AffectedScope
                {
                    ProjectId = this.Scope.Project.ProjectId,
                    SceneId = this.Scene.Id,
                    SceneName = this.Scene.Name,
                    AssetVirtualPath = this.SceneInput.AssetUri.VirtualPath,
                    ComponentName = SceneStartingPropertyPaths.IsStartingProperty(propertyPath) ? "Play" : "Environment",
                },
                SuggestedAction = new PrimaryAction
                {
                    ActionId = "Cook.GoToProperty",
                    Label = "Go to property",
                    Kind = PrimaryActionKind.Custom,
                    Payload = new Dictionary<string, string>(StringComparer.Ordinal)
                    {
                        ["AssetUri"] = this.SceneInput.AssetUri.ToUriString(),
                        ["PropertyPath"] = propertyPath,
                    },
                },
            });
    }
}
