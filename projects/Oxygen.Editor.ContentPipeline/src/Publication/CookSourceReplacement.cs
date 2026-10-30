// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A reviewed retained source whose private replacement participates in cook publication.</summary>
/// <param name="BundleRoot">The cook path of the existing bundle's folder, such as <c>/Content/Models/Ship</c>.</param>
/// <param name="Before">The complete source and settings baseline accepted by the user.</param>
internal sealed record CookSourceReplacement(string BundleRoot, CookRootImage Before);
