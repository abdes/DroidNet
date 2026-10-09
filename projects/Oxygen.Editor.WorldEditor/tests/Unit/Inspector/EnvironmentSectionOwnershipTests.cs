// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Inspector;

[TestClass]
public sealed class EnvironmentSectionOwnershipTests
{
    [TestMethod]
    public async Task SectionEdits_ShareOneSceneGestureAndCanonicalDiagnostics()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        _ = model.Backdrop.EditOwner.Should().BeSameAs(model.EditOwner);
        _ = model.SkyAtmosphere.EditOwner.Should().BeSameAs(model.EditOwner);
        _ = model.Exposure.EditOwner.Should().BeSameAs(model.EditOwner);
        _ = model.PostProcessing.EditOwner.Should().BeSameAs(model.EditOwner);
        _ = model.AtmosphereLights.EditOwner.Should().BeSameAs(model.EditOwner);
        var original = fixture.Scene.Environment.SkyAtmosphere.PlanetRadiusMeters;
        model.SkyAtmosphere.EditOwner.BeginEditSession("PlanetRadiusKm", NumberBoxEditInteractionKind.PointerDrag);
        model.SkyAtmosphere.PlanetRadiusKm = 6400;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        model.SkyAtmosphere.EditOwner.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Scene.Environment.SkyAtmosphere.PlanetRadiusMeters.Should().Be(original);
        _ = model.SkyAtmosphere.PlanetRadiusKm.Should().Be(original / 1000);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.Exposure.CurveEditor.Diagnostic.Should().BeSameAs(model.Exposure.AutoExposureCompensationCurveDiagnostic);
    }

    [TestMethod]
    public async Task HiddenSections_RejectLateInputWithoutCreatingAnotherEditOwner()
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = new EnvironmentViewModel(fixture.Commands, () => fixture.Context);
        model.SetScene(fixture.Scene);
        var owner = model.EditOwner;
        var sky = model.SkyAtmosphere;
        var sources = model.AtmosphereLights;
        var primary = sources.PrimaryAtmosphereSource;
        var original = fixture.Scene.Environment;
        model.SetInputEnabled(false);
        sky.PlanetRadiusKm = 6400;
        model.Backdrop.SolidColorR = 0.25f;
        model.Exposure.ManualExposureEv = 8;
        await model.PendingEdits.ConfigureAwait(false);
        _ = fixture.Scene.Environment.Should().Be(original);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        model.SetInputEnabled(true);
        model.SetScene(null);
        model.SetScene(fixture.Scene);
        _ = model.EditOwner.Should().BeSameAs(owner);
        _ = model.SkyAtmosphere.Should().BeSameAs(sky);
        _ = model.AtmosphereLights.Should().BeSameAs(sources);
        _ = sources.PrimaryAtmosphereSource.Should().BeSameAs(primary);
        _ = sky.PlanetRadiusKm.Should().Be(original.SkyAtmosphere.PlanetRadiusMeters / 1000);
    }
}
