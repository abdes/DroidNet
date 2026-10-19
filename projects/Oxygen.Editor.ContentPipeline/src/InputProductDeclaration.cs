// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Where an action or a mapping context is declared.</summary>
/// <param name="File">The input source file.</param>
/// <param name="Source">The source as read.</param>
/// <param name="Position">Where the product's name is declared.</param>
public sealed record InputProductDeclaration(FileSystemPath File, InputSource Source, SourcePosition Position);
