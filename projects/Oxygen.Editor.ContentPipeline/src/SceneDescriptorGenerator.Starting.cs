// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Input;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
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
    /// <returns>The camera's node; <see langword="null"/> when the scene has no camera or the authored one is
    /// missing, which is reported.</returns>
    private static NativeNodeRef? CreateStartingCamera(Scene scene, IReadOnlyDictionary<NodeId, int> nodeIndices, PropertySite site)
    {
        if (scene.StartingCamera is { } authored && scene.EffectiveStartingCamera is null)
        {
            var name = scene.AllNodes.FirstOrDefault(node => node.Id == authored.Node)?.Metadata.Name;
            var message = (name, authored.IsOwned) switch
            {
                (null, _) => "The starting camera was deleted. Pick another camera in the scene's Play section, or return it to Automatic.",
                (_, true) => $"The starting camera '{name}' is no longer a camera. Pick another camera in the scene's Play section, or return it to Automatic.",
                _ => $"The starting camera's instance root '{name}' no longer instances a scene. Pick another camera in the scene's Play section, or return it to Automatic.",
            };
            site.Report(SceneStartingPropertyPaths.StartingCamera, message);
            return null;
        }

        return scene.EffectiveStartingCamera is { } camera ? NodeRef(camera, nodeIndices) : null;
    }

    /// <summary>Writes a node reference as the cooked scene names a node: its index, and the path below it when an
    /// instance brings the node in.</summary>
    /// <param name="reference">The reference; its node is in <paramref name="nodeIndices"/>.</param>
    /// <param name="nodeIndices">Each node's index in the cooked scene.</param>
    /// <returns>The cooked reference.</returns>
    private static NativeNodeRef NodeRef(NodeReference reference, IReadOnlyDictionary<NodeId, int> nodeIndices)
        => new(nodeIndices[reference.Node], NodePathOf(reference.Path));

    /// <summary>Writes a node path as the cooked scene does, or <see langword="null"/> for the empty path.</summary>
    /// <param name="path">The path.</param>
    /// <returns>Each node ID in lowercase canonical form.</returns>
    private static IReadOnlyList<string>? NodePathOf(NodePath path)
        => path.IsEmpty ? null : [.. path.Nodes.Select(static node => node.ToString())];

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
            var source = StartingContextRules.Describe(scope.Project, entry.Context, container => InputSourceFiles.Read(scope.Project, container));
            if (StartingContextRules.Check(entry, source) is { } problem)
            {
                site.Report(SceneStartingPropertyPaths.StartingInputContexts, problem.Message);
                continue;
            }

            contexts.Add(new NativeStartingInputContext(
                ContentPipelinePaths.ToNativeDescriptorPath(entry.Context, AssetFileTypes.InputMappingContext),
                StartingContextRules.EffectivePriority(entry, source)));
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
