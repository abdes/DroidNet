// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Messages;

/// <summary>Notifies scene-following panels that the accepted editable graph has been retired.</summary>
/// <param name="DocumentId">The retired document identity.</param>
public sealed record SceneAuthoringUnloadedMessage(DocumentId DocumentId);
