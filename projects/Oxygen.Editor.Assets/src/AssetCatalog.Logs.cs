// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Assets;

/// <inheritdoc cref="AssetCatalog"/>
public sealed partial class AssetCatalog
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "An observer of the asset catalog failed after the catalog was updated; the update stays in place.")]
    private partial void LogObserverFailed(Exception exception);
}
