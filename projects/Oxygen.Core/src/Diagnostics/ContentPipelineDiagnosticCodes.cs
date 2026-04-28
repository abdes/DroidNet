// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for content-pipeline orchestration.
/// </summary>
public static class ContentPipelineDiagnosticCodes
{
    /// <summary>
    /// Scene descriptor generation failed.
    /// </summary>
    public const string SceneDescriptorGenerationFailed =
        DiagnosticCodes.ContentPipelinePrefix + "SCENE.DescriptorGenerationFailed";

    /// <summary>
    /// Authored scene field is not represented by the ED-M07 native descriptor slice.
    /// </summary>
    public const string SceneUnsupportedField =
        DiagnosticCodes.ContentPipelinePrefix + "SCENE.UnsupportedField";

    /// <summary>
    /// Procedural geometry descriptor generation failed.
    /// </summary>
    public const string GeometryDescriptorGenerationFailed =
        DiagnosticCodes.ContentPipelinePrefix + "GEOMETRY.DescriptorGenerationFailed";

    /// <summary>
    /// Import manifest generation failed.
    /// </summary>
    public const string ManifestGenerationFailed =
        DiagnosticCodes.ContentPipelinePrefix + "MANIFEST.GenerationFailed";

    /// <summary>
    /// Loose cooked output inspection failed.
    /// </summary>
    public const string InspectFailed = DiagnosticCodes.ContentPipelinePrefix + "INSPECT.Failed";

    /// <summary>
    /// Loose cooked output validation failed.
    /// </summary>
    public const string ValidateFailed = DiagnosticCodes.ContentPipelinePrefix + "VALIDATE.Failed";
}
