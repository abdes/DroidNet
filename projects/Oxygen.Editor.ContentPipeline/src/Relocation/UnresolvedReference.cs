// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A reference of an authored file that does not resolve in the project's source mounts.</summary>
/// <param name="File">The referencing file: a descriptor, a scene, an input source or a model import record.</param>
/// <param name="Property">The field's JSON path in the file, such as <c>$.parameters.base_color_texture</c>.</param>
/// <param name="Written">The reference as the file writes it.</param>
/// <param name="Resolved">The virtual path it resolves to; <see langword="null"/> when the reference grammar refuses it.</param>
/// <param name="Code">Why it does not resolve: <c>OXE.ASSETID.Resolve.Missing</c> when nothing is at the path,
/// <c>mount.unbound</c> or <c>mount.folder_missing</c> when its mount is not available on this machine.</param>
public sealed record UnresolvedReference(FileSystemPath File, string Property, string Written, string? Resolved, string Code);
