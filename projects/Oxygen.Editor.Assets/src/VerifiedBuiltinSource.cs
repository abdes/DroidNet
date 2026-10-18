// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <summary>Associates a verified project contribution with its engine-provided identity.</summary>
/// <param name="Root">The physical published root covered by provenance.</param>
/// <param name="Identity">The native contribution identity.</param>
/// <param name="Origin">The engine catalog row that produced this contribution.</param>
public sealed record VerifiedBuiltinSource(FileSystemPath Root, AssetPath Identity, AssetEntry Origin);
