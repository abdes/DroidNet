// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport.Native;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Integration.UI.Tests.Scenes;

/// <summary>Checks on the real engine that the preview shows the open scene document and follows its edits.</summary>
[TestClass]
[TestCategory("Integration")]
public sealed class ScenePreviewEngineTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task OpenedSceneBecomesReadyAndAnEditReachesTheEngine() => EnqueueAsync(async () =>
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        var engine = await SharedEngine.EnsureRunningAsync(timeout.Token).ConfigureAwait(true);
        try
        {
            using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\LivePreviewTests"));
            using var preview = new ScenePreview(engine, documents.Open);
            var built = new Scene { Name = "Test Scene" };
            built.RootNodes.Add(new SceneNode(built, "Cube"));
            var document = await documents.OpenAsync(built.Dehydrate()).ConfigureAwait(true);
            await PreviewWaits.UntilReadyAsync(preview, document.Id, timeout.Token).ConfigureAwait(true);
            var node = SceneContent.Of(document).Scene.RootNodes.Single();

            var move = new PropertyEdit.Builder().Set(SceneSchema.Transform.PositionX, 7f).Build();
            var result = await SceneContent.Of(document).Authoring.ExecuteAsync(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [node.Id] = move }, "Move")).ConfigureAwait(true);

            _ = result.Succeeded.Should().BeTrue();
            var target = preview.ShownTargetOf(document.Id);
            _ = target.Should().NotBeNull();
            _ = await PreviewWaits.UntilNodeAsync(
                engine,
                target!.Value,
                node.Id,
                state => state.Properties.Any(static value => value.ComponentId == RuntimeComponentId.Transform && value.FieldId == (ushort)TransformField.PositionX && MathF.Abs(value.Value - 7f) < 0.001f),
                timeout.Token).ConfigureAwait(true);
            _ = SharedEngine.Results.Should().NotContain(static value => value.Severity == Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error);
        }
        finally
        {
            await SharedEngine.ResetAsync().ConfigureAwait(true);
        }
    });
}
