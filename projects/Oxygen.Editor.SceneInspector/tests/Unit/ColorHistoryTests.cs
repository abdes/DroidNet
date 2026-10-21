// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.SceneInspector.TestSupport;
using static Oxygen.Editor.SceneInspector.TestSupport.SceneTestData;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
public sealed class ColorHistoryTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Color samples commit once or cancel completely, preserving undo and redo.</summary>
    /// <param name="kind">The inspector whose edit session is exercised.</param>
    /// <param name="samples">The number of preview samples.</param>
    /// <param name="cancel">Whether to cancel the edit session.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Environment", 1, false)]
    [DataRow("Environment", 3, false)]
    [DataRow("Environment", 3, true)]
    [DataRow("Light", 1, false)]
    [DataRow("Light", 3, false)]
    [DataRow("Light", 3, true)]
    public async Task InspectorColorEditSessionPreservesHistory(string kind, int samples, bool cancel)
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = CreateColorModel(kind, fixture);
        if (model is SceneSettingsViewModel environment)
        {
            environment.Backdrop.SetSolidColor(Vector3.One);
            await environment.PendingEdits.ConfigureAwait(true);
        }

        fixture.Document.History.Clear();
        var before = ReadSceneColor(fixture, kind);
        var owner = (IInspectorEditSessionOwner)model;
        owner.BeginEditSession(model is SceneSettingsViewModel ? "SolidColor" : "Color", NumberBoxEditInteractionKind.PointerDrag);
        var preview = default(Vector3);
        for (var sample = 1; sample <= samples; sample++)
        {
            preview = new Vector3(0.15f * sample, 0.25f, 0.5f);
            if (model is SceneSettingsViewModel background)
            {
                background.Backdrop.SetSolidColor(preview);
            }
            else
            {
                ((DirectionalLightViewModel)model).SetColor(preview);
            }
        }

        await PendingColorEdits(model).ConfigureAwait(true);
        _ = ReadSceneColor(fixture, kind).Should().NotBe(before);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty("preview samples belong to one uncommitted edit session");
        owner.EndEditSession(cancel ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);
        await PendingColorEdits(model).ConfigureAwait(true);
        var expected = cancel ? before : preview;
        AssertColorClose(ReadSceneColor(fixture, kind), expected);
        _ = fixture.Document.History.UndoStack.Should().HaveCount(cancel ? 0 : 1);
        if (!cancel)
        {
            await fixture.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = ReadSceneColor(fixture, kind).Should().Be(before);
            await fixture.Document.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            AssertColorClose(ReadSceneColor(fixture, kind), expected);
        }
    }

    private static Task PendingColorEdits(IDisposable model) => model is SceneSettingsViewModel environment ? environment.PendingEdits : ((DirectionalLightViewModel)model).PendingEdits;

    private static IDisposable CreateColorModel(string kind, SceneAuthoringFixture fixture)
    {
        if (string.Equals(kind, "Environment", StringComparison.Ordinal))
        {
            var model = new SceneSettingsViewModel(() => fixture.Authoring);
            model.SetScene(fixture.Scene);
            return model;
        }

        var light = new DirectionalLightViewModel(() => fixture.Authoring);
        light.UpdateValues([fixture.Node]);
        return light;
    }
}
