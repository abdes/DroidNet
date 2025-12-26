// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Cook;

public sealed record GeometryCookInput(AssetKey AssetKey, ImportedGeometry Geometry, GeometryData Data);
