// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Cube import settings for a single source image.</summary>
/// <param name="Layout">How the image lays out the faces.</param>
/// <param name="FaceSize">The face size in pixels for a panorama, a multiple of 256; ignored otherwise.</param>
public sealed record TextureCubeImport(CubeLayout Layout, int? FaceSize = null);
