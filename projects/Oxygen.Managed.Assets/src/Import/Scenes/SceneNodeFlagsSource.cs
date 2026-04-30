// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Import.Scenes;

public sealed record SceneNodeFlagsSource(
    bool Visible = true,
    bool Static = false,
    bool CastsShadows = true,
    bool ReceivesShadows = true,
    bool RayCastingSelectable = true,
    bool IgnoreParentTransform = false);
