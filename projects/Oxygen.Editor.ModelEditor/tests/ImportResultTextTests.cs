// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ModelEditor.Settings;
using static Oxygen.Editor.ModelEditor.TestSupport.ImportDialogs;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// The Result row says what the import does to a model in Oxygen's space (right-handed, Z up, metres): glTF is in
/// metres and Y up by specification; an FBX file's unit and axes are its own, centimetres and Y up when it states none.
/// </summary>
[TestClass]
public sealed class ImportResultTextTests
{
    private static readonly JsonElement Defaults = JsonSerializer.SerializeToElement(new Dictionary<string, object>());

    [TestMethod]
    public void AGltfFileKeepsItsSizeAndTurnsToZUp()
        => _ = ImportResultText.Describe(Facts("glb"), Defaults).Should()
            .Be("Real size kept: glTF is in metres, as Oxygen is. Turned from Y up to Oxygen's Z up.");

    [TestMethod]
    public void AnFbxFileInCentimetresIsConvertedToMetres()
        => _ = ImportResultText.Describe(Facts("fbx"), Defaults).Should()
            .Be("Real size kept: converted from centimetres to Oxygen's metres (1 file unit = 0.01 m). Already Z up, as Oxygen is.");

    [TestMethod]
    public void KeepingFileUnitsSaysHowMuchLargerTheModelIs()
        => _ = ImportResultText.Describe(Facts("fbx"), Settings(("unit_policy", "preserve"))).Should()
            .StartWith("Not converted, so 100× larger than its real size: 1 file unit (0.01 m) = 1 m in Oxygen.");

    [TestMethod]
    public void AnFbxFileWithoutUnitOrAxesSaysWhatIsAssumed()
    {
        var facts = Facts("fbx") with { UnitMeters = null, Up = "unknown", IsLeftHanded = null };

        _ = ImportResultText.Describe(facts, Defaults).Should()
            .Be("The file states no unit, so it is read as centimetres and converted to Oxygen's metres (1 file unit = 0.01 m). The file states no axes: read as Y up and turned to Oxygen's Z up.");
    }

    [TestMethod]
    public void ALeftHandedFileIsMirroredAndAChosenUpAxisWins()
    {
        var leftHanded = Facts("fbx") with { Up = "+Y", IsLeftHanded = true };

        _ = ImportResultText.Describe(leftHanded, Defaults).Should().EndWith("Turned from Y up to Oxygen's Z up, and mirrored along forward to be right-handed.");
        _ = ImportResultText.Describe(leftHanded, Settings(("up_axis", "z"))).Should().EndWith("Read as Z up, as chosen; Oxygen is Z up too.");
        _ = ImportResultText.Describe(Facts("gltf"), Settings(("unit_policy", "custom"), ("unit_scale", 2.5))).Should().StartWith("Made 2.5× its real size, as chosen.");
    }

    private static JsonElement Settings(params (string Key, object Value)[] values)
        => JsonSerializer.SerializeToElement(values.ToDictionary(static value => value.Key, static value => value.Value, StringComparer.Ordinal));
}
