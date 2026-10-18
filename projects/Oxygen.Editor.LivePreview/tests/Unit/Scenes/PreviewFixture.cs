// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Interop;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// A scene preview over the real scene kind and a fake engine: a test opens scene documents, drives the engine and
/// reads the commands the preview sent and the results it published.
/// </summary>
/// <remarks>
/// A document opens while the engine is ready, so opening sends nothing; the test then puts the engine in the state it
/// arranges and synchronizes the scene it chose. The world rejects the property and background commands a test names.
/// </remarks>
internal sealed partial class PreviewFixture : IDisposable
{
    private readonly List<Document> opened = [];

    /// <summary>Initializes a new instance of the <see cref="PreviewFixture"/> class.</summary>
    /// <param name="state">The engine's state.</param>
    public PreviewFixture(EngineServiceState state = EngineServiceState.Running)
    {
        this.Engine = new FakeEngine(state);
        this.World.OnExecute = this.Decide;
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Add);
        this.Preview = new ScenePreview(this.Engine, this.Documents.Open, operationResults: publisher.Object);
    }

    public SceneDocuments Documents { get; } = new();

    public FakeEngine Engine { get; }

    public FakeWorld World => this.Engine.World;

    public ScenePreview Preview { get; }

    public List<RuntimeWorldRequest> Requests => this.World.Requests;

    public List<OperationResult> Results { get; } = [];

    /// <summary>Sets the engine's state, without a state change notification.</summary>
    public EngineServiceState State
    {
        set => this.Engine.SetState(value);
    }

    /// <summary>Gets or sets the component whose property commands the engine rejects.</summary>
    public EngineComponentId? RejectedPropertyComponent { get; set; }

    /// <summary>Gets or sets the background color whose environment command the engine rejects.</summary>
    public Vector3? RejectedBackground { get; set; }

    public static SceneAuthoring AuthoringOf(Document document) => SceneContent.Of(document).Authoring;

    public static Scene SceneOf(Document document) => SceneContent.Of(document).Scene;

    public void Dispose()
    {
        this.Preview.Dispose();
        this.Documents.Dispose();
    }

    /// <summary>Opens a scene with one node, a cube at an X position.</summary>
    /// <param name="x">The cube's X position.</param>
    /// <returns>The document's scene, its cube and the document.</returns>
    public async Task<(Scene Scene, SceneNode Node, Document Document)> OpenCubeAsync(float x)
    {
        var scene = this.Documents.NewScene(string.Create(CultureInfo.InvariantCulture, $"Scene {this.opened.Count + 1}"));
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        node.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(x, 0, 0);
        var document = await this.OpenAsync(scene).ConfigureAwait(false);
        var shown = SceneOf(document);
        return (shown, shown.AllNodes.Single(candidate => candidate.Id == node.Id), document);
    }

    /// <summary>Opens a scene while the engine is ready, so opening projects nothing, and keeps the arranged state.</summary>
    /// <param name="scene">The scene the document's file records.</param>
    /// <returns>The document.</returns>
    public async Task<Document> OpenAsync(Scene scene)
    {
        var state = this.Engine.State;
        this.Engine.SetState(EngineServiceState.Ready);
        try
        {
            var document = await this.Documents.OpenAsync(scene).ConfigureAwait(false);
            this.opened.Add(document);
            return document;
        }
        finally
        {
            this.Engine.SetState(state);
        }
    }

    /// <summary>Holds the first node creation until the test completes it; later creations are accepted.</summary>
    /// <returns>The held request, and the completion that releases it.</returns>
    public (TaskCompletionSource<RuntimeWorldRequest> Request, TaskCompletionSource<RuntimeCommandResult> Completion) DelayNodeCreation()
    {
        var request = new TaskCompletionSource<RuntimeWorldRequest>(TaskCreationOptions.RunContinuationsAsynchronously);
        var completion = new TaskCompletionSource<RuntimeCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.World.OnCreateNode = value => request.TrySetResult(value) ? completion.Task : Task.FromResult(FakeWorld.Accept(value));
        return (request, completion);
    }

    /// <summary>Accepts every node creation at once again.</summary>
    public void ResumeNodeCreation() => this.World.OnCreateNode = null;

    private RuntimeCommandResult? Decide(RuntimeWorldRequest request)
        => request.Command is RuntimeSetEnvironment environment && this.RejectedBackground == environment.Background.ColorRgb
            ? FakeWorld.Result(request, RuntimeCommandStatus.Rejected, "Controlled background rejection")
            : request.Command is RuntimeSetProperties properties && properties.Entries.Any(entry => entry.ComponentId == this.RejectedPropertyComponent)
            ? FakeWorld.Result(request, RuntimeCommandStatus.Rejected, "Controlled property rejection")
            : null;
}
