// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Editable material document state.
/// </summary>
/// <param name="DocumentId">The editor document identity.</param>
/// <param name="MaterialUri">The material source asset URI.</param>
/// <param name="MaterialGuid">The editor-side stable material identity derived from <paramref name="MaterialUri"/>.</param>
/// <param name="SourcePath">The absolute source descriptor path.</param>
/// <param name="DisplayName">The material display name.</param>
/// <param name="Source">The editable source snapshot.</param>
/// <param name="Asset">The identity-bearing asset, if the catalog provided one.</param>
/// <param name="IsDirty">Whether the editable snapshot has unsaved changes.</param>
/// <param name="CookState">The last known cook state.</param>
public sealed record MaterialDocument(
    Guid DocumentId,
    Uri MaterialUri,
    Guid MaterialGuid,
    string SourcePath,
    string DisplayName,
    MaterialSource Source,
    MaterialAsset? Asset,
    bool IsDirty,
    MaterialCookState CookState);
