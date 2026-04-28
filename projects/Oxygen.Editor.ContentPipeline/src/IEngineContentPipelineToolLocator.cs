// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Locates native content-pipeline tools used by bounded fallbacks.
/// </summary>
public interface IEngineContentPipelineToolLocator
{
    /// <summary>
    /// Gets the Oxygen import tool executable path.
    /// </summary>
    /// <returns>The import tool path.</returns>
    public string GetImportToolPath();
}
