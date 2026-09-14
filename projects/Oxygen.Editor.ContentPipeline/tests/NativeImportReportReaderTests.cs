// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks native output ownership before it can be used for publication.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeImportReportReaderTests
{
    /// <summary>Native output files and source diagnostics remain correlated to their owning operation.</summary>
    [TestMethod]
    public void ReadsExactOutputsAndSourceDiagnostics()
    {
        var (execution, report) = CreateReport();
        var result = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.OutputFiles.Should().Equal("Models/Model/Geometry/Triangle.ogeo");
        _ = result.Diagnostics.Should().ContainSingle(issue => issue.OperationId == execution.OperationId
            && issue.Severity == DiagnosticSeverity.Warning && issue.Code == "source.warning"
            && issue.AffectedPath == Path.Combine(execution.InputRoot, "model.gltf"));
    }

    /// <summary>Malformed versions, job identity and escaped paths cannot become authoritative output ownership.</summary>
    /// <param name="invalid">The invalid report field.</param>
    [TestMethod]
    [DataRow("version")]
    [DataRow("index")]
    [DataRow("path")]
    [DataRow("type")]
    public void RejectsInvalidReportOwnership(string invalid)
    {
        var (execution, report) = CreateReport();
        switch (invalid)
        {
            case "version": report["report_version"] = "1"; break;
            case "index": report["jobs"]![0]!["index"] = 2; break;
            case "path": report["jobs"]![0]!["outputs"]![0]!["path"] = "../outside.ogeo"; break;
            default: report["jobs"]![0]!["type"] = "fbx"; break;
        }

        Action read = () => _ = NativeImportReportReader.Read(report.ToJsonString(), execution, 0);
        _ = read.Should().Throw<InvalidDataException>();
    }

    private static (ContentImportExecution execution, JsonNode report) CreateReport()
    {
        var root = Path.Combine(Path.GetTempPath(), "NativeReport", Guid.NewGuid().ToString("N"));
        var output = Path.Combine(root, "output");
        var execution = new ContentImportExecution(
            Guid.NewGuid(),
            Path.Combine(root, "inputs"),
            root,
            new(1, output, new("/Content"), [new("model", "gltf", "model.gltf", [], Output: null, Name: "Model")]));
        var report = JsonNode.Parse("""
            {"report_version":"2","session":{"cooked_root":""},"jobs":[
              {"index":1,"type":"gltf","status":"succeeded","outputs":[{"path":"Models/Model/Geometry/Triangle.ogeo","size_bytes":128}],
               "diagnostics":[{"severity":"warning","code":"source.warning","message":"Source warning."}]}]}
            """)!;
        report["session"]!["cooked_root"] = output;
        return (execution, report);
    }
}
