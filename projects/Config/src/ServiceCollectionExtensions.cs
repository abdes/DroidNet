// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

using System.IO.Abstractions;
using DroidNet.Config.Detail;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;

/// <summary>
/// Extends the IServiceCollection interface with a method that makes it possible to configure writable options (i.e.
/// configuration settings that can be modified and persisted).
/// </summary>
/// <see cref="IWritableOptions{T}" />
public static class ServiceCollectionExtensions
{
    /* TODO(abdes): Make it explicit that the serialization uses JSON, or support a more open API */

    public static IServiceCollection ConfigureWritable<T>(
        this IServiceCollection services,
        IConfigurationSection section,
        string filePath)
        where T : class, new()
    {
        _ = services.Configure<T>(section)
            .AddTransient<IWritableOptions<T>>(
                provider =>
                {
                    // All the next services are necessarily present as they are
                    // injected by the host itself.
                    var configuration = (IConfigurationRoot)provider.GetRequiredService<IConfiguration>();
                    var options = provider.GetRequiredService<IOptionsMonitor<T>>();
                    var fs = provider.GetRequiredService<IFileSystem>();

                    return new WritableOptions<T>(options, configuration, section.Key, filePath, fs);
                });
        return services;
    }
}
