// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Presentation-neutral RGBA preview color for material picker rows.
/// </summary>
/// <param name="R">Red channel in the range 0..1.</param>
/// <param name="G">Green channel in the range 0..1.</param>
/// <param name="B">Blue channel in the range 0..1.</param>
/// <param name="A">Alpha channel in the range 0..1.</param>
public sealed record MaterialPreviewColor(float R, float G, float B, float A);
