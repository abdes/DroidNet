// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

/// <summary>The native Inspector's script report maps to typed declarations, and malformed reports are refused.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ScriptMetadataTests
{
    private const string Key = "10203040-5060-7080-90a0-b0c0d0e0f001";
    private static readonly AssetPath Script = AssetPath.Parse("/.cooked/Scripts/rotate.oscript");

    [TestMethod]
    public void CookedDeclarationsKeepOrderTypesDefaultsAndAssetKinds()
    {
        var report = CookedScriptReport.Parse(Report("""
            { "name": "speed", "type": "float", "default": 0.9 },
            { "name": "count", "type": "int32", "default": 3 },
            { "name": "armed", "type": "bool", "default": true },
            { "name": "mode", "type": "string", "default": "walk" },
            { "name": "offset", "type": "vec3", "default": [1, 2, 3] },
            { "name": "fire", "type": "asset", "asset_type": 5 },
            { "name": "target", "type": "node" }
            """));

        var declarations = report.SingleOrDefault(Script)!;

        _ = declarations.ScriptUri.Should().Be(Script);
        _ = declarations.NativeScriptKey.Should().Be(Guid.Parse(Key));
        _ = declarations.Parameters.Should().Equal(
            new CookedScriptParameter("speed", CookedScriptParameterKind.Float, 0.9f, 0),
            new CookedScriptParameter("count", CookedScriptParameterKind.Int32, 3, 0),
            new CookedScriptParameter("armed", CookedScriptParameterKind.Bool, true, 0),
            new CookedScriptParameter("mode", CookedScriptParameterKind.String, "walk", 0),
            new CookedScriptParameter("offset", CookedScriptParameterKind.Vec3, new Vector3(1, 2, 3), 0),
            new CookedScriptParameter("fire", CookedScriptParameterKind.Asset, null, 5),
            new CookedScriptParameter("target", CookedScriptParameterKind.Node, null, 0));
    }

    [TestMethod]
    public void AnEmptyReportHasNoDeclarations()
        => _ = CookedScriptReport.Parse("""{ "schema_version": 1, "scripts": [] }""").SingleOrDefault(Script).Should().BeNull();

    [TestMethod]
    public void AReportOutsideTheSchemaOrRepeatingAParameterIsRefused()
    {
        var unknownType = () => CookedScriptReport.Parse(Report("""{ "name": "speed", "type": "matrix" }"""));
        var missingDefault = () => CookedScriptReport.Parse(Report("""{ "name": "speed", "type": "float" }"""));
        var repeated = () => CookedScriptReport.Parse(Report("""
            { "name": "speed", "type": "float", "default": 1 },
            { "name": "speed", "type": "int32", "default": 1 }
            """));

        _ = unknownType.Should().Throw<InvalidDataException>();
        _ = missingDefault.Should().Throw<InvalidDataException>();
        _ = repeated.Should().Throw<InvalidDataException>();
    }

    private static string Report(string parameters)
        => $$"""
            { "schema_version": 1, "scripts": [
              { "virtual_path": "/.cooked/Scripts/rotate.oscript", "asset_key": "{{Key}}", "parameters": [ {{parameters}} ] }
            ] }
            """;
}
