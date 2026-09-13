// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>A labeled, read-only asset fact.</summary>
/// <param name="Label">The relationship or fact name.</param>
/// <param name="Value">The known value.</param>
public sealed record AssetInformationFact(string Label, string Value);
