// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ImporterRevisionsTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The importers come from the ImportTool's own report of its versions.</summary>
    [TestMethod]
    public void ParsesTheImportToolVersions()
    {
        var revisions = ImporterRevisions.Parse("""
            {"program":"Oxygen.Cooker.ImportTool","version":"0.1","formats":{"pak":10},"assets":{"geometry":6},
             "importers":{"gltf":2,"texture":1}}
            """);

        _ = revisions.Of("gltf").Should().Be(2U);
        _ = revisions.Of("texture").Should().Be(1U);
    }

    /// <summary>A tool that reports no importers cannot say which revision cooked a product.</summary>
    [TestMethod]
    public void RejectsVersionsWithoutImporters()
    {
        Action parse = () => _ = ImporterRevisions.Parse("""{"program":"Oxygen.Cooker.Inspector","version":"0.1","formats":{}}""");

        _ = parse.Should().Throw<InvalidDataException>();
    }

    /// <summary>An importer the tool does not have cannot have cooked a product.</summary>
    [TestMethod]
    public void RejectsAnUnknownImporter()
    {
        Action read = () => _ = ImporterRevisionsFixture.Current.Of("obj");

        _ = read.Should().Throw<InvalidDataException>().WithMessage("*'obj'*");
    }

    /// <summary>The installed tool reports its revisions once: later reads of the same tool run no worker.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ReadsTheInstalledToolOnce()
    {
        using var workspace = new ImportAdapterWorkspace();
        var runner = new VersionsRunner(new(0, ImporterRevisionsFixture.VersionsJson, string.Empty));
        var api = ImportAdapterScenario.CreateQueryApi(workspace, runner);

        var first = await api.ReadImporterRevisionsAsync(artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await api.ReadImporterRevisionsAsync(artifacts: null, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = first.Revisions.Should().BeEquivalentTo(ImporterRevisionsFixture.Current.Revisions);
        _ = second.Should().BeSameAs(first);
        _ = runner.Runs.Should().Be(1);
        _ = runner.Arguments.Should().Equal("versions", "--json");
    }

    /// <summary>A tool that cannot report its versions fails the read with what it printed.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ReportsAToolThatCannotReportItsVersions()
    {
        using var workspace = new ImportAdapterWorkspace();
        var api = ImportAdapterScenario.CreateQueryApi(workspace, new VersionsRunner(new(1, string.Empty, "unknown command")));

        Func<Task> read = () => api.ReadImporterRevisionsAsync(artifacts: null, this.TestContext.CancellationToken);

        _ = await read.Should().ThrowAsync<InvalidDataException>().WithMessage("*unknown command*").ConfigureAwait(false);
    }

    private sealed class VersionsRunner(ContentPipelineProcessResult result) : IContentPipelineProcessRunner
    {
        public int Runs { get; private set; }

        public IReadOnlyList<string> Arguments { get; private set; } = [];

        public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Runs++;
            this.Arguments = request.Arguments;
            return Task.FromResult(result);
        }
    }
}
