// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class NumericGesturesTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;
    [TestMethod]
    [DataRow("SunAzimuth")]
    [DataRow("SunElevation")]
    public Task SunAngleAuthoringRoundTripsDisplayDegrees(string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var inspector = fixture.CreateInspectorHost("Light");
        var model = inspector.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        if (string.Equals(field, "SunAzimuth", StringComparison.Ordinal))
        {
            model.SunAzimuth = 12;
        }
        else
        {
            model.SunElevation = 12;
        }

        await model.PendingEdits.ConfigureAwait(true);
        _ = (string.Equals(field, "SunAzimuth", StringComparison.Ordinal) ? model.SunAzimuth : model.SunElevation).Should().BeApproximately(12, 0.001f);
    });
    /// <summary>A hundred bound samples produce one history entry and throttled previews followed by a terminal sync.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlGroupsSamplesAndHistoryRefreshesTheInspector(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = ResolveNumericView(model, field);
        var number = await this.LoadNumericFieldAsync(view, field).ConfigureAwait(true);
        _ = number.Should().NotBeNull();
        var before = number.NumberValue;
        using var throttle = new SceneEngineSync(Mock.Of<IEngineService>());
        var previews = new List<DateTimeOffset>();
        var terminals = 0;
        fixture.ConfigureObservedSync(throttle, previews, () => terminals++);
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = before + (sample / 100f);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = terminals.Should().Be(1);
        _ = previews.Should().NotBeEmpty();
        _ = previews.Zip(previews.Skip(1), (first, second) => second - first).Should().OnlyContain(interval => interval >= TimeSpan.FromMilliseconds(16));
        _ = number.NumberValue.Should().Be(before + 1);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before + 1);
    });

    /// <summary>Cancellation restores a numeric preview without making a history entry.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlCancellationRestoresTheBoundModel(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = ResolveNumericView(model, field);
        var number = await this.LoadNumericFieldAsync(view, field).ConfigureAwait(true);
        var before = number.NumberValue;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = before + 1;
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before + 1);
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Cancel);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Wheel samples share a session until the idle delay has elapsed.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlWheelCommitsOnlyAfterIdle(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = ResolveNumericView(model, field);
        var number = await this.LoadNumericFieldAsync(view, field).ConfigureAwait(true);
        var before = number.NumberValue;
        for (var tick = 1; tick <= 4; tick++)
        {
            RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.MouseWheel);
            number.NumberValue = before + tick;
            RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(before + 4);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });

    private static UserControl ResolveNumericView(IPropertyEditor<SceneNode> model, string field) => model switch
    {
        EnvironmentViewModel environment => field switch
        {
            "PlanetRadiusKm" or "AtmosphereHeightKm" => new SkyAtmosphereSectionView { ViewModel = environment.SkyAtmosphere },
            _ => new EnvironmentView { ViewModel = environment },
        },
        _ => CreateNumericView(model),
    };

    private async Task<NumberBox> LoadNumericFieldAsync(UserControl view, string field)
    {
        var scroller = new ScrollViewer { Width = 340, Height = 480, Content = view, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
        var root = new Grid();
        root.Children.Add(scroller);
        await LoadTestContentAsync(root).ConfigureAwait(true);

        foreach (var disclosure in view.FindDescendants().OfType<Expander>())
        {
            disclosure.IsExpanded = true;
        }

        return (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => view.FindDescendant<NumberBox>(input => Equals(input.Tag, field)),
            field,
            this.TestContext.CancellationToken).ConfigureAwait(true);
    }
}
