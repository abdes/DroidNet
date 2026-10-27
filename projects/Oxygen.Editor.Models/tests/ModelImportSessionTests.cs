// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// An import session is an ImportTool import manifest: one gltf or fbx job per model in order, sources relative to the
/// manifest when they can be, the settings in the job's own keys, the run's settings at the top, and no output (model
/// editor LLD section 7, ME-15).
/// </summary>
[TestClass]
public sealed class ModelImportSessionTests
{
    private const string ManifestPath = @"C:\Project\Vehicles.import-manifest.json";

    [TestMethod]
    public void ASessionRoundTripsThroughAnImportManifest()
    {
        var session = new ModelImportSession
        {
            Jobs =
            [
                new(@"C:\Sources\Car.fbx", AssetPath.Parse("/Content/Vehicles/Car/Car.fbx"), Settings(("name", "Car"), ("up_axis", "z"))),
                new(@"D:\Downloads\Truck.glb", AssetPath.Parse("/Content/Vehicles/Truck/Truck.glb"), Settings(("name", "Truck"))),
            ],
            Run = new() { MaxInFlightJobs = 2 },
        };

        var bytes = session.ToBytes(ManifestPath);
        var read = ModelImportSession.Read(ManifestPath, bytes);

        _ = read.Jobs.Select(static job => job.Source).Should().Equal(@"C:\Sources\Car.fbx", @"D:\Downloads\Truck.glb");
        _ = read.Jobs.Select(static job => job.VirtualPath).Should().Equal(session.Jobs.Select(static job => job.VirtualPath));
        _ = read.Jobs.Select(static job => job.Name).Should().Equal("Car", "Truck");
        _ = read.Jobs[0].Settings.GetProperty("up_axis").GetString().Should().Be("z");
        _ = read.Run.MaxInFlightJobs.Should().Be(2);
        _ = read.SkippedJobs.Should().BeEmpty();
    }

    [TestMethod]
    public void AManifestHasNoOutputAndWritesSourcesRelativeWhenItCan()
    {
        var session = new ModelImportSession
        {
            Jobs =
            [
                new(@"C:\Sources\Car.fbx", AssetPath.Parse("/Content/Vehicles/Car/Car.fbx"), Settings(("name", "Car"))),
                new(@"D:\Downloads\Truck.glb", AssetPath.Parse("/Content/Vehicles/Truck/Truck.glb"), Settings(("name", "Truck"))),
            ],
        };

        using var document = JsonDocument.Parse(session.ToBytes(ManifestPath));
        var jobs = document.RootElement.GetProperty("jobs");

        _ = document.RootElement.TryGetProperty("output", out _).Should().BeFalse("the command line names its cooked root");
        _ = jobs[0].GetProperty("type").GetString().Should().Be("fbx");
        _ = jobs[0].GetProperty("source").GetString().Should().Be("../Sources/Car.fbx");
        _ = jobs[1].GetProperty("type").GetString().Should().Be("gltf");
        _ = jobs[1].GetProperty("source").GetString().Should().Be("D:/Downloads/Truck.glb");
        _ = jobs[1].GetProperty("virtual_path").GetString().Should().Be("/Content/Vehicles/Truck/Truck.glb");
    }

    [TestMethod]
    public void JobsThatAreNotModelsAreLeftOutAndNamed()
    {
        const string manifest = """
            {
              "version": 1,
              "jobs": [
                { "id": "crate", "type": "gltf", "source": "Crate.gltf", "virtual_path": "/Content/Props/Crate/Crate.gltf", "name": "Crate", "import_cameras": false },
                { "id": "albedo", "type": "texture", "source": "Albedo.png", "virtual_path": "/Content/Textures/Albedo.otex" }
              ]
            }
            """;

        var read = ModelImportSession.Read(ManifestPath, Encoding.UTF8.GetBytes(manifest));

        _ = read.Jobs.Should().ContainSingle();
        _ = read.Jobs[0].Source.Should().Be(@"C:\Project\Crate.gltf", "a relative source resolves beside the manifest");
        _ = read.Jobs[0].Settings.GetProperty("import_cameras").GetBoolean().Should().BeFalse();
        _ = read.Jobs[0].Settings.TryGetProperty("id", out _).Should().BeFalse("job keys are not import settings");
        _ = read.SkippedJobs.Should().Equal("albedo");
    }

    [TestMethod]
    public void AFileThatIsNotAnImportManifestIsRefused()
    {
        var notJson = () => ModelImportSession.Read(ManifestPath, Encoding.UTF8.GetBytes("Car.fbx"));
        var offSchema = () => ModelImportSession.Read(ManifestPath, Encoding.UTF8.GetBytes("""{ "version": 1 }"""));

        _ = notJson.Should().Throw<InvalidDataException>();
        _ = offSchema.Should().Throw<InvalidDataException>();
    }

    private static JsonElement Settings(params (string Key, string Value)[] values)
        => JsonSerializer.SerializeToElement(values.ToDictionary(static value => value.Key, static value => value.Value, StringComparer.Ordinal));
}
