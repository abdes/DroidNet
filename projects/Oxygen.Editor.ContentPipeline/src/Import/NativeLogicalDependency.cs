// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A native output or logical reference.</summary>
/// <param name="VirtualPath">The native logical identity.</param>
/// <param name="Kind">The schema-defined asset/resource kind.</param>
/// <param name="ObjectPath">The source field that declared it.</param>
/// <param name="Required">Whether native cooking requires this dependency/output.</param>
public sealed record NativeLogicalDependency(string VirtualPath, string Kind, string ObjectPath, bool Required);
