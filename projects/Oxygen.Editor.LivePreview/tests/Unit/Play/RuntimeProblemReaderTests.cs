// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Play;

/// <summary>
/// Reading the script diagnostics a running player publishes: rendered with the source line of the project file they
/// name, or, without a source the renderer can show, from their code, severity and slot.
/// </summary>
[TestClass]
[TestCategory("Play")]
public sealed class RuntimeProblemReaderTests
{
    private const string Script = "local Script = oxygen.script {}\nlocal x = nil\nx.position = 1\nreturn Script\n";

    private const string Root = @"C:\Project";

    private readonly MockFileSystem files = new();
    private readonly RuntimeProblemReader sut;

    public RuntimeProblemReaderTests()
    {
        this.sut = new RuntimeProblemReader(TestProject.Create(this.files, Root).Context);
    }

    [TestMethod]
    [DataRow("error", DiagnosticSeverity.Error, "error")]
    [DataRow("warning", DiagnosticSeverity.Warning, "warning")]
    [DataRow("info", DiagnosticSeverity.Info, "note")]
    public void Read_WithoutRenderableSource_FallsBackToTheCodeSeverityAndSlot(string severity, DiagnosticSeverity expected, string label)
    {
        var diagnostic = JsonNode.Parse($$"""
            { "version": 1, "severity": "{{severity}}", "code": "S9999", "message": "m", "labels": [], "notes": [],
              "slot": { "node": "Player", "slot_index": 2, "hook": "on_update" } }
            """)!.AsObject();

        var problem = this.sut.Read(diagnostic);

        _ = (problem.Severity, problem.Code, problem.SlotNode, problem.SlotIndex, problem.SlotHook)
            .Should().Be((expected, "S9999", "Player", 2, "on_update"));
        _ = (problem.File, problem.Line, problem.Column).Should().Be((null, null, null));
        _ = problem.Rendered.Should().StartWith($"{label}[S9999]");
    }

    [TestMethod]
    public void Read_WithoutCodeOrSlot_UsesPlaceholdersAndNoSlot()
    {
        var problem = this.sut.Read([]);

        _ = (problem.Severity, problem.Code, problem.SlotNode, problem.SlotIndex, problem.SlotHook)
            .Should().Be((DiagnosticSeverity.Info, "unknown", null, null, null));
    }

    [TestMethod]
    public void Read_SpanInAProjectScript_ResolvesTheFileAndPosition()
    {
        var file = this.Write("Content/move.lua", Script);
        var diagnostic = JsonNode.Parse("""
            { "version": 1, "severity": "error", "code": "S0701", "family": "runtime", "message": "attempt to index nil",
              "labels": [ { "span": { "source": "/Content/move.lua", "begin": { "line": 3, "column": 1 }, "end": { "line": 3, "column": 2 } },
                "message": "this is nil", "primary": true } ],
              "notes": [] }
            """)!.AsObject();

        var problem = this.sut.Read(diagnostic);

        _ = (problem.File, problem.Line, problem.Column, problem.Code, problem.Severity)
            .Should().Be((file, 3, 1, "S0701", DiagnosticSeverity.Error));
        _ = problem.Rendered.Should().Contain("x.position = 1");
    }

    [TestMethod]
    public void ResolveSource_VirtualMountRelativeAndProjectRelativePaths_FindTheFile()
    {
        var inMount = this.Write("Content/move.lua", Script);
        var inProject = this.Write("Tools/helper.lua", Script);

        _ = (this.sut.ResolveSource("/Content/move.lua"), this.sut.ResolveSource("move.lua"), this.sut.ResolveSource("Tools/helper.lua"))
            .Should().Be((inMount, inMount, inProject));
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("  ")]
    [DataRow("gone.lua")]
    [DataRow("/Content/gone.lua")]
    [DataRow("/Missing/move.lua")]
    public void ResolveSource_MissingFileOrBlankName_ReturnsNull(string sourceName)
    {
        _ = this.Write("Content/move.lua", Script);

        _ = this.sut.ResolveSource(sourceName).Should().BeNull();
    }

    private string Write(string relativePath, string text)
    {
        var path = this.files.Path.GetFullPath(this.files.Path.Combine(Root, relativePath));
        _ = this.files.Directory.CreateDirectory(this.files.Path.GetDirectoryName(path)!);
        this.files.File.WriteAllText(path, text);
        return path;
    }
}
