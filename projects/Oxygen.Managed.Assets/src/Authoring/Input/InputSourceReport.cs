// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>What <see cref="InputSourceReader.Check"/> found in an input source.</summary>
/// <param name="Source">The source, or <see langword="null"/> when it is not JSON or does not match the schema.</param>
/// <param name="Problems">Every problem, each at its position when known.</param>
public sealed record InputSourceReport(InputSource? Source, IReadOnlyList<AssetProblem> Problems);
