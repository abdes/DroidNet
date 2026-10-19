// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Authoring;

/// <summary>Where something is in an authored file, for go to declaration and for problems.</summary>
/// <param name="Line">The one-based line.</param>
/// <param name="Column">The one-based column, in characters.</param>
public readonly record struct SourcePosition(int Line, int Column);
