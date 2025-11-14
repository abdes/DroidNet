// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.InteropServices;
using AwesomeAssertions;
using DryIoc;
using Microsoft.Windows.ApplicationModel.DynamicDependency;

namespace DroidNet.Resources.Tests;

[TestClass]
[DoNotParallelize]
[ExcludeFromCodeCoverage]
public sealed class ResourceMapProviderTests
{
    [TestInitialize]
    public void TestInitialize()
    {
        ClearResourceProviderCaches();
        ResetResourceExtensionsProvider();
        InitializeBootstrapperOrInconclusive();
    }

    [TestCleanup]
    public void TestCleanup()
    {
        ClearResourceProviderCaches();
        ResetResourceExtensionsProvider();
        Bootstrap.Shutdown();
    }

    [TestMethod]
    public void GetAssemblyResourceMap_CachesByAssemblyName()
    {
        var assembly = typeof(ResourceMapProviderTests).Assembly;

        var first = ResourceMapProvider.GetAssemblyResourceMap(assembly);
        var second = ResourceMapProvider.GetAssemblyResourceMap(assembly);

        _ = second.Should().BeSameAs(first);
    }

    [TestMethod]
    public void GetAssemblyResourceMap_LoadsPriBesideAssembly()
    {
        EnsureBootstrapInitialized();

        var assembly = typeof(ResourceMapProviderTests).Assembly;
        var assemblyDirectory = Path.GetDirectoryName(assembly.Location) ?? string.Empty;
        var priPath = Path.Combine(assemblyDirectory, "DroidNet.Resources.Tests.pri");
        _ = File.Exists(priPath).Should().BeTrue("Test PRI payload must be deployed next to the test assembly.");

        var resourceMap = ResourceMapProvider.GetAssemblyResourceMap(assembly);

        var thanks = GetValueFromSubtree(resourceMap, "Localized", "MSG_Thanks");
        _ = thanks.Should().Be("Thanks");

        var secret = GetValueFromSubtree(resourceMap, "Special", "MSG_SecretMessage");
        _ = secret.Should().Be("A secrete message from a special place");
    }

    [TestMethod]
    public void DryIoc_WithLocalization_ResolvesResourcesFromPri()
    {
        EnsureBootstrapInitialized();
        ResetResourceExtensionsProvider();

        using var container = new Container();
        var returned = container.WithLocalization();

        _ = returned.Should().BeSameAs(container);

        var localized = "MSG_Thanks".GetLocalized(typeof(ResourceMapProviderTests).Assembly);
        _ = localized.Should().Be("Thanks");

        var special = "Special/MSG_SecretMessage".R<ResourceMapProviderTests>();
        _ = special.Should().Be("A secrete message from a special place");
    }

    private static void EnsureBootstrapInitialized()
    {
        InitializeBootstrapperOrInconclusive();
    }

    private static void InitializeBootstrapperOrInconclusive()
    {
        if (TryInitializeBootstrap())
        {
            return;
        }

        Assert.Inconclusive("Windows App SDK bootstrapper is not available on this machine.");
    }

    private static bool TryInitializeBootstrap()
    {
        var version = typeof(Bootstrap).Assembly.GetName().Version;
        if (version is null)
        {
            return false;
        }

        var majorMinor = ((uint)version.Major << 16) | (ushort)version.Minor;

        try
        {
            Bootstrap.Initialize(majorMinor);
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (COMException ex)
        {
            const int AlreadyInitialized = unchecked((int)0x800401F0);
            const int AlreadyRunning = unchecked((int)0x80040014);
            if (ex.HResult is AlreadyInitialized or AlreadyRunning)
            {
                return true;
            }

            return false;
        }
    }

    private static void ClearResourceProviderCaches()
    {
        ClearConcurrentDictionary<string, IResourceMap>("AssemblyMapCache");
        ClearConcurrentDictionary<string, byte>("AssemblyWarningCache");
    }

    private static void ClearConcurrentDictionary<TKey, TValue>(string fieldName)
        where TKey : notnull
    {
        var field = typeof(ResourceMapProvider).GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Static);
        if (field?.GetValue(null) is ConcurrentDictionary<TKey, TValue> cache)
        {
            cache.Clear();
        }
    }

    private static string? GetValueFromSubtree(IResourceMap map, string subtreePath, string key)
    {
        var current = map;
        if (!string.IsNullOrEmpty(subtreePath))
        {
            var segments = subtreePath.Split('/', StringSplitOptions.RemoveEmptyEntries);
            foreach (var segment in segments)
            {
                current = current.GetSubtree(segment);
            }
        }

        return current.TryGetValue(key, out var value) ? value : null;
    }

    private static void ResetResourceExtensionsProvider()
    {
        var providerField = typeof(ResourceExtensions).GetField("provider", BindingFlags.NonPublic | BindingFlags.Static);
        providerField?.SetValue(null, null);
    }
}
