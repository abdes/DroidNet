// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Renames or moves the output group of a model import within its type folders.</summary>
/// <param name="SourcePath">The virtual path of the model's primary source file.</param>
/// <param name="Group">The new group, relative to each type folder; it may be nested, such as <c>Vehicles/Car</c>.</param>
public sealed record ImportGroupMove(string SourcePath, string Group);
