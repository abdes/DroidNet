// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class SavedSourceScenario
{
    internal static CookDocumentState SavedState(string path) => new(Guid.NewGuid(), path, Path.GetFileName(path), 1, 1, IsDirty: false, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));
    internal static CapturingEngineContentPipelineApi CreateSuccessfulApi(
        CookWorkspace workspace,
        Func<ContentSourceAnalysisExecution, CancellationToken, Task<NativeSourceAnalysisReport>> sourceAnalysis)
        => new(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
    {
        SourceAnalysis = sourceAnalysis
    };
}
