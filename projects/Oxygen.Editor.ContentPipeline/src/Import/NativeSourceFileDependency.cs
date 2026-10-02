// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A file declared by native source preparation.</summary>
/// <param name="Path">The absolute logical file path.</param>
/// <param name="Required">Whether a missing file prevents cooking.</param>
public sealed record NativeSourceFileDependency(string Path, bool Required);
