// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeImportReportReaderTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Native output files, products and source diagnostics remain correlated to their owning operation.</summary>
    [TestMethod]
    public void ReadsExactOutputsProductsAndSourceDiagnostics()
    {
        var (execution, report) = CreateReport();
        var result = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.OutputsBySource["model.gltf"].Should().Equal("Models/Model/Geometry/Triangle.ogeo");
        _ = result.ProductsBySource["model.gltf"].Should().BeEquivalentTo("/Content/Models/Model/Geometry/Triangle.ogeo", "/Content/Models/Model/Textures/Albedo.otex");
        _ = result.Diagnostics.Should().ContainSingle(issue => issue.OperationId == execution.OperationId
            && issue.Severity == DiagnosticSeverity.Warning && issue.Code == "source.warning"
            && issue.AffectedPath == Path.Combine(execution.InputRoot, "model.gltf"));
    }

    /// <summary>A model import reports what it established, which the publication commits to the model's record.</summary>
    [TestMethod]
    public void ReadsWhatAModelImportEstablished()
    {
        var (execution, report) = CreateModelReport(includeUpdate: true);

        var result = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);

        _ = result.Succeeded.Should().BeTrue();
        var update = result.ModelImports["model.gltf"];
        _ = update.GetProperty("surface_provenance").GetProperty("schema_version").GetInt32().Should().Be(2);
        _ = update.GetProperty("published_nodes").GetArrayLength().Should().Be(1);
        _ = update.GetProperty("source_fingerprint").GetProperty("files").GetArrayLength().Should().Be(0);
    }

    /// <summary>A successful model import that omits what it established cannot update the model's record.</summary>
    [TestMethod]
    public void RejectsASuccessfulModelImportWithoutItsUpdate()
    {
        var (execution, report) = CreateModelReport(includeUpdate: false);

        Action read = () => _ = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);

        _ = read.Should().Throw<InvalidDataException>();
    }

    /// <summary>A failed model import establishes nothing.</summary>
    [TestMethod]
    public void ReportsNoModelImportWhenTheBatchFails()
    {
        var (execution, report) = CreateModelReport(includeUpdate: true);
        report["jobs"]![0]!["status"] = "failed";

        var result = NativeImportReportReader.Read(report.ToJsonString(), execution, 1);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.ModelImports.Should().BeEmpty();
    }

    /// <summary>Malformed versions, job identity, escaped paths and products that are not identities cannot become authoritative output ownership.</summary>
    /// <param name="invalid">The invalid report field.</param>
    [TestMethod]
    [DataRow("version")]
    [DataRow("index")]
    [DataRow("path")]
    [DataRow("type")]
    [DataRow("products")]
    [DataRow("product")]
    [DataRow("descriptor")]
    [DataRow("key")]
    public void RejectsInvalidReportOwnership(string invalid)
    {
        var (execution, report) = CreateReport();
        switch (invalid)
        {
            case "version": report["report_version"] = "1"; break;
            case "index": report["jobs"]![0]!["index"] = 2; break;
            case "path": report["jobs"]![0]!["outputs"]![0]!["path"] = "../outside.ogeo"; break;
            case "products": _ = report["jobs"]![0]!.AsObject().Remove("products"); break;
            case "product": report["jobs"]![0]!["products"]![0]!["virtual_path"] = "/Content/Models/../Triangle.ogeo"; break;
            case "descriptor": report["jobs"]![0]!["products"]![0]!["descriptor_relative_path"] = "../outside.ogeo"; break;
            case "key": _ = report["jobs"]![0]!["products"]![0]!.AsObject().Remove("asset_key"); break;
            default: report["jobs"]![0]!["type"] = "fbx"; break;
        }

        Action read = () => _ = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);
        _ = read.Should().Throw<InvalidDataException>();
    }

    private static (ContentImportExecution Execution, JsonNode Report) CreateReport()
    {
        var root = Path.Combine(Path.GetTempPath(), "NativeReport", Guid.NewGuid().ToString("N"));
        var output = Path.Combine(root, "output");
        var execution = new ContentImportExecution(
            Guid.NewGuid(),
            Path.Combine(root, "inputs"),
            root,
            new(1, output, new("/Content"), [new("model", "gltf", "model.gltf", [], Output: null, Name: "Model")]));
        var report = JsonNode.Parse("""
            {"report_version":"3","session":{"cooked_root":""},"jobs":[
              {"index":1,"type":"gltf","status":"succeeded","outputs":[{"path":"Models/Model/Geometry/Triangle.ogeo","size_bytes":128}],
               "products":[
                 {"kind":"asset","virtual_path":"/Content/Models/Model/Geometry/Triangle.ogeo","descriptor_relative_path":"Models/Model/Geometry/Triangle.ogeo","asset_key":"00000000-0000-0000-0000-000000000001","asset_type":2},
                 {"kind":"texture","virtual_path":"/Content/Models/Model/Textures/Albedo.otex","descriptor_relative_path":"Models/Model/Textures/Albedo.otex"}],
               "diagnostics":[{"severity":"warning","code":"source.warning","message":"Source warning."}]}]}
            """)!;
        report["session"]!["cooked_root"] = output;
        return (execution, report);
    }

    private static (ContentImportExecution Execution, JsonNode Report) CreateModelReport(bool includeUpdate)
    {
        var root = Path.Combine(Path.GetTempPath(), "NativeReport", Guid.NewGuid().ToString("N"));
        var output = Path.Combine(root, "output");
        var execution = new ContentImportExecution(
            Guid.NewGuid(),
            Path.Combine(root, "inputs"),
            root,
            new(1, output, new("/Content"), [new("model", "model", "model.gltf", [], Output: null, Name: null) { Record = "imports/00000000-0000-7000-8000-000000000001.import.json" }]));
        var update = includeUpdate
            ? """
              ,"model_import":{"surface_provenance":{"schema_version":2,"geometries":[]},
                "published_nodes":[{"anchor":"Body","node_id":"00000000-0000-7000-8000-000000000002"}],
                "source_fingerprint":{"size":2,"sha256":"%HASH%","files":[]}}
              """.Replace("%HASH%", new string('a', 64), StringComparison.Ordinal)
            : string.Empty;

        // The engine reports a model job as the gltf or fbx job its record runs.
        var report = JsonNode.Parse("""
            {"report_version":"3","session":{"cooked_root":""},"jobs":[
              {"index":1,"type":"gltf","status":"succeeded","outputs":[],"products":[],"diagnostics":[]
            """ + update + "}]}")!;
        report["session"]!["cooked_root"] = output;
        return (execution, report);
    }
}
