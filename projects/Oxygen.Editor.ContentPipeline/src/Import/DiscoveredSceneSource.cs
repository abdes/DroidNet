// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Native source facts coupled to the exact primary bytes that produced the dependency layout.</summary>
/// <param name="Bundle">The files and relative paths for coherent retention.</param>
/// <param name="Inspection">Native source features and coordinate conversion facts.</param>
public sealed record DiscoveredSceneSource(ImportSourceBundle Bundle, SceneSourceInspectionReport Inspection);
