// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets;

/// <summary>
/// Provides constant values used throughout the Oxygen Assets module.
/// </summary>
public static class AssetPipelineConstants
{
    /// <summary>
    /// The name of the folder where cooked assets are stored.
    /// </summary>
    public const string CookedFolderName = ".cooked";

    /// <summary>
    /// The name of the folder where imported asset metadata is stored.
    /// </summary>
    public const string ImportedFolderName = ".imported";

    /// <summary>
    /// The name of the loose cooked index file.
    /// </summary>
    public const string IndexFileName = "container.index.bin";
}
