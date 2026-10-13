// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Where an input action or mapping context is declared.
/// </summary>
/// <param name="SourcePath">The input file.</param>
/// <param name="Line">The 1-based line of the declaration's name.</param>
public sealed record InputDeclaration(string SourcePath, int Line);
