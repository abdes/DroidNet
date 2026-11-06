// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>One reference of an authored file: the field that holds it, and the identity it names.</summary>
/// <param name="Property">The field's JSON path in the file, such as <c>$.parameters.base_color_texture</c>.</param>
/// <param name="Identity">The referenced asset identity.</param>
/// <param name="Written">The reference as the file writes it.</param>
internal sealed record AuthoredReference(string Property, AssetPath Identity, string Written);

/// <summary>A reference of an authored file that the reference grammar refuses.</summary>
/// <param name="Property">The field's JSON path in the file.</param>
/// <param name="Written">The reference as the file writes it.</param>
/// <param name="Code">The refusal's code.</param>
internal sealed record RefusedReference(string Property, string Written, string Code);
