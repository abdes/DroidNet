// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of native scene descriptor generation.
/// </summary>
public sealed record SceneDescriptorGenerationResult(
    Uri SceneAssetUri,
    string DescriptorPath,
    string DescriptorVirtualPath,
    IReadOnlyList<ContentCookInput> Dependencies,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
