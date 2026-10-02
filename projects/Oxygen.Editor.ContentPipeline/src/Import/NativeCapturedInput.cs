// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>One source identity and the input state admitted for native execution.</summary>
/// <param name="LogicalPath">Absolute authored identity used for naming and relative references.</param>
/// <param name="Exists">Presence observed during capture.</param>
/// <param name="Metadata">Original metadata, when observed.</param>
/// <param name="File">Private captured bytes; null for presence-only or absent inputs.</param>
public sealed record NativeCapturedInput(string LogicalPath, bool Exists, NativeSourceFileMetadata? Metadata, NativeCapturedFile? File);
