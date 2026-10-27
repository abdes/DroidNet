// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>A root binding and the index it serves.</summary>
/// <param name="Binding">The published root.</param>
/// <param name="Document">The root's index.</param>
/// <param name="Folder">The root's physical folder.</param>
public sealed record PublishedRoot(CookPublicationRoot Binding, Document Document, FileSystemPath Folder);
