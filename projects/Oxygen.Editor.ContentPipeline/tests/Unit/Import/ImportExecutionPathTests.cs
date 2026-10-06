// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportExecutionPathTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Relative execution roots cannot silently depend on the editor's current directory.</summary>
    /// <param name="invalidPart">The execution field that lacks an absolute path or identity.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow("input")]
    [DataRow("operation")]
    [DataRow("output")]
    [DataRow("identity")]
    public async Task InvalidExecutionPathsDoNotLaunchWorker(string invalidPart)
    {
        using var workspace = new ImportAdapterWorkspace();
        var execution = CreateExecution(workspace, CreateManifest(workspace));
        execution = invalidPart switch
        {
            "input" => execution with { InputRoot = "inputs" },
            "operation" => execution with { OperationRoot = "operation" },
            "output" => execution with { Manifest = execution.Manifest with { Output = "output" } },
            _ => execution with { OperationId = Guid.Empty },
        };
        var runner = new CapturingRunner(new(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator("unused.exe"), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);
        Func<Task> import = () => api.ImportAsync(execution, this.TestContext.CancellationToken);

        _ = await import.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = runner.Request.Should().BeNull();
        _ = Directory.Exists(execution.OperationRoot).Should().BeFalse();
    }
}
