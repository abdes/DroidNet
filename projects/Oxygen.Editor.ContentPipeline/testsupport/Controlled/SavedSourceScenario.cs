// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class SavedSourceScenario
{
    internal static CapturingEngineContentPipelineApi CreateSuccessfulApi(
        CookWorkspace workspace,
        Func<ContentSourceAnalysisExecution, CancellationToken, Task<NativeSourceAnalysisReport>> sourceAnalysis)
        => new(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            SourceAnalysis = sourceAnalysis,
        };
}
