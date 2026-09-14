// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A reviewed retained source whose private replacement participates in cook publication.</summary>
/// <param name="BundleName">The existing directory beneath Content/SourceMedia/DCC.</param>
/// <param name="Before">The complete source and settings baseline accepted by the user.</param>
internal sealed record CookSourceReplacement(string BundleName, CookRootImage Before);
