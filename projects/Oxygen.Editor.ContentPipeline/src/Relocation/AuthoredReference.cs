// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>One reference of an authored file: the field that holds it, and the identity it names.</summary>
/// <param name="Property">The field's JSON path in the file, such as <c>$.parameters.base_color_texture</c>.</param>
/// <param name="Identity">The referenced asset identity.</param>
internal sealed record AuthoredReference(string Property, AssetPath Identity);
