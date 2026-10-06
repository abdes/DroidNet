// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class ImportAdapterScenario
{
    internal static ContentImportExecution CreateExecution(ImportAdapterWorkspace workspace, ContentImportManifest manifest)
    {
        var operationId = Guid.NewGuid();
        return new(operationId, workspace.Root, Path.Combine(workspace.Root, ".build", "cook", operationId.ToString("N")), manifest);
    }

    internal static ContentImportManifest CreateManifest(ImportAdapterWorkspace workspace) => new(Version: 1, Output: Path.Combine(workspace.Root, ".cooked", "Content"), Layout: new ContentImportLayout("/Content"), Jobs: [new ContentImportJob(Id: "material-red", Type: "material-descriptor", Source: "Content/Materials/Red.omat.json", DependsOn: [], Output: null, Name: "Red"),]);

    internal static ContentSourceAnalysisExecution AnalysisExecution(ImportAdapterWorkspace workspace) => new(Guid.NewGuid(), workspace.Root, Path.Combine(workspace.Root, "operation"), [new("first", "material-descriptor", "first.json", [], null, null), new("second", "material-descriptor", "second.json", [], null, null)]);

    internal static ImportToolContentPipelineApi CreateQueryApi(ImportAdapterWorkspace workspace, IContentPipelineProcessRunner runner) => new(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);
}
