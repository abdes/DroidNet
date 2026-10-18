// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>What a cube texture descriptor cooks.</summary>
/// <param name="StoresRadiance">
/// Whether the cube keeps float (HDR) texels. Only such a cube can light a scene; an LDR cube is
/// display-only.
/// </param>
public readonly record struct CubeDescriptorInfo(bool StoresRadiance);
