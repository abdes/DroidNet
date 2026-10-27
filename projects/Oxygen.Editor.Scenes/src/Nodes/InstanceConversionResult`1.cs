// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>What <see cref="InstanceConversion"/> made, or why it made nothing.</summary>
/// <typeparam name="T">What it makes.</typeparam>
/// <param name="Value">What it made, or <see langword="null"/> when refused.</param>
/// <param name="Refusal">Why it made nothing, or <see langword="null"/> when it made <paramref name="Value"/>.</param>
public sealed record InstanceConversionResult<T>(T? Value, SceneDiagnostic? Refusal)
    where T : class;
