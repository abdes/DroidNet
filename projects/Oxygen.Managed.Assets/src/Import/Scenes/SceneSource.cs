// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record SceneSource(
    string Schema,
    string Name,
    IReadOnlyList<SceneNodeSource> Nodes);
