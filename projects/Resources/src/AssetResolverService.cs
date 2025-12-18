// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Windows.ApplicationModel;
using Windows.Storage;

namespace DroidNet.Resources;

/// <summary>
///     Provides services for resolving asset URIs based on relative paths and assembly context.
/// </summary>
/// <param name="loggerFactory">
///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
public partial class AssetResolverService(ILoggerFactory loggerFactory)
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<AssetResolverService>() ?? NullLoggerFactory.Instance.CreateLogger<AssetResolverService>();

    /// <summary>
    ///     Resolves an asset URI from a relative path and the calling assembly context.
    /// </summary>
    /// <param name="relativePath">The relative path to the asset, typically under the "Assets" directory.</param>
    /// <param name="caller">The <see cref="Assembly"/> from which the asset is being requested.</param>
    /// <returns>A <see cref="Uri"/> to the asset if found; otherwise, <see langword="null"/>.</returns>
    public Uri? FromRelativePath(string relativePath, Assembly caller)
    {
        Uri? uri;
        var assemblyName = caller.GetName().Name;

        if (IsPackaged())
        {
            _ = TryPackagedAssetAtPath($"Assets/{relativePath}", out uri)
                    || (assemblyName is not null && TryPackagedAssetAtPath($"{assemblyName}/Assets/{relativePath}", out uri));
            this.LogIconAsset(isPackaged: true, relativePath, uri);
            return uri;
        }

        _ = TryUnpackagedAssetAtPath($"Assets/{relativePath}", out uri)
            || (assemblyName is not null && TryUnpackagedAssetAtPath($"{assemblyName}/Assets/{relativePath}", out uri));
        this.LogIconAsset(isPackaged: false, relativePath, uri);

        return uri;

        [DebuggerNonUserCode]
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "just testing for packaged or not")]
        static bool IsPackaged()
        {
            try
            {
                _ = Package.Current;
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        bool TryPackagedAssetAtPath(string relativePath, out Uri? uri)
        {
            var candidateUri = new Uri($"ms-appx:///{relativePath}");

            // This would work for packaged apps
            var root = Package.Current.InstalledLocation;

            // WinRT's StorageFolder.TryGetItemAsync is so fucking stupid and needs the separator to be `\\`
            relativePath = relativePath.Replace("/", "\\", StringComparison.OrdinalIgnoreCase);
            var item = root.TryGetItemAsync(relativePath).GetAwaiter().GetResult();
            uri = item is StorageFile ? candidateUri : null;
            return uri is not null;
        }

        bool TryUnpackagedAssetAtPath(string relativePath, out Uri? uri)
        {
            try
            {
                var fullPath = Path.Combine(
                    AppContext.BaseDirectory,
                    relativePath.Replace('/', Path.DirectorySeparatorChar));
                if (Path.Exists(fullPath))
                {
                    uri = new Uri(fullPath);
                }
                else
                {
                    this.LogAssetFileNotFound(isPackaged: false, relativePath);
                    uri = null;
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                this.LogAssetFileNotFound(isPackaged: false, relativePath, ex);
                uri = null;
            }
#pragma warning restore CA1031 // Do not catch general exception types

            return uri is not null;
        }
    }
}
