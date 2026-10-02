// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;
using Color = Windows.UI.Color;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed partial class ColorHistoryTests
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
        if (model is EnvironmentViewModel environment)
        {
            environment.SetBackgroundColor(Color.FromArgb(255, 255, 255, 255));
            await environment.PendingEdits.ConfigureAwait(true);
        }

        fixture.Context.History.Clear();
        var before = ReadSceneColor(fixture, kind);
        var owner = (IInspectorEditSessionOwner)model;
        owner.BeginEditSession(model is EnvironmentViewModel ? "BackgroundColor" : "Color", NumberBoxEditInteractionKind.PointerDrag);
        var preview = default(Color);
        for (var sample = 1; sample <= samples; sample++)
        {
            preview = Color.FromArgb(255, (byte)(48 * sample), 80, 160);
            if (model is EnvironmentViewModel background)
            {
                background.SetBackgroundColor(preview);
            }
            else
            {
                ((DirectionalLightViewModel)model).SetColor(preview);
            }
        }

        await PendingColorEdits(model).ConfigureAwait(true);
        _ = ReadSceneColor(fixture, kind).Should().NotBe(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty("preview samples belong to one uncommitted edit session");
        owner.EndEditSession(cancel ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);
        await PendingColorEdits(model).ConfigureAwait(true);
        var expected = cancel ? before : model is EnvironmentViewModel ? DecodeSrgb(preview) : new Vector3(preview.R / 255f, preview.G / 255f, preview.B / 255f);
        AssertColorClose(ReadSceneColor(fixture, kind), expected);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(cancel ? 0 : 1);
        if (!cancel)
        {
            await fixture.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = ReadSceneColor(fixture, kind).Should().Be(before);
            await fixture.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            AssertColorClose(ReadSceneColor(fixture, kind), expected);
        }
    }

    private static Vector3 DecodeSrgb(Color color)
    {
        static float Decode(byte channel)
        {
            var encoded = channel / 255.0;
            return (float)(encoded <= 0.04045 ? encoded / 12.92 : Math.Pow((encoded + 0.055) / 1.055, 2.4));
        }

        return new(Decode(color.R), Decode(color.G), Decode(color.B));
    }

    private static Task PendingColorEdits(IDisposable model) => model is EnvironmentViewModel environment ? environment.PendingEdits : ((DirectionalLightViewModel)model).PendingEdits;
    private static IDisposable CreateColorModel(string kind, SceneAuthoringFixture fixture)
    {
        if (string.Equals(kind, "Environment", StringComparison.Ordinal))
        {
            var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
            model.SetScene(fixture.Scene);
            return model;
        }

        var light = new DirectionalLightViewModel(fixture.Commands, () => fixture.Context);
        light.UpdateValues([fixture.Node]);
        return light;
    }
}
