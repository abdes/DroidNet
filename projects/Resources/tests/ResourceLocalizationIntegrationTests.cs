// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.InteropServices;
using AwesomeAssertions;
using DroidNet.Resources.Generator.Localized_3c672ab6;
using DryIoc;
using Microsoft.Windows.ApplicationModel.DynamicDependency;

namespace DroidNet.Resources.Tests;

[TestClass]
[DoNotParallelize]
[ExcludeFromCodeCoverage]
public sealed class ResourceLocalizationIntegrationTests
{
    [ClassCleanup]
    public static void ClassCleanup()
    {
        ClearResourceProviderCaches();
    }

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
    public void Container_WithLocalization_ComposesWorkingEnvironment()
    {
        EnsureBootstrapInitialized();
        EnsurePriPayloadPresent();

        using var container = new Container();
        var returned = container.WithLocalization();

        _ = returned.Should().BeSameAs(container);

        var provider = container.Resolve<IResourceMapProvider>();
        var assembly = typeof(ResourceLocalizationIntegrationTests).Assembly;
        var assemblyLocalizedMap = provider.GetAssemblyResourceMap(assembly).GetSubtree("Localized");
        const string ThanksKey = "MSG_Thanks";
        const string SecretKey = "Special/MSG_SecretMessage";

        var thanksFromProvidedMap = ThanksKey.GetLocalized(assemblyLocalizedMap);
        var thanksFromAssembly = ThanksKey.GetLocalized(assembly);
        var thanksFromTyped = ThanksKey.GetLocalized<ResourceLocalizationIntegrationTests>();
        var thanksFromL = ThanksKey.L();
        var thanksFromLTyped = ThanksKey.L<ResourceLocalizationIntegrationTests>();
        var thanksFromR = ThanksKey.R();
        var thanksFromRTyped = ThanksKey.R<ResourceLocalizationIntegrationTests>();

        _ = thanksFromProvidedMap.Should().Be("Thanks");
        _ = thanksFromAssembly.Should().Be("Thanks");
        _ = thanksFromTyped.Should().Be("Thanks");
        _ = thanksFromL.Should().Be("Thanks");
        _ = thanksFromLTyped.Should().Be("Thanks");
        _ = thanksFromR.Should().Be("Thanks");
        _ = thanksFromRTyped.Should().Be("Thanks");

        var specialFromR = SecretKey.R();
        var specialFromRTyped = SecretKey.R<ResourceLocalizationIntegrationTests>();

        _ = specialFromR.Should().Be("A secrete message from a special place");
        _ = specialFromRTyped.Should().Be("A secrete message from a special place");
    }

    [TestMethod]
    public void ResourceExtensions_Lookup_UsesAssemblyAndApplicationFallbacks()
    {
        EnsureBootstrapInitialized();
        EnsurePriPayloadPresent();

        using var container = new Container();
        _ = container.WithLocalization();

        var assembly = typeof(ResourceLocalizationIntegrationTests).Assembly;

        var thanks = "MSG_Thanks".GetLocalized(assembly);
        _ = thanks.Should().Be("Thanks");

        var goodbye = "MSG_Goodbye".GetLocalized(assembly);
        _ = goodbye.Should().Be("Goodbye from MyApp");
    }

    private static void EnsurePriPayloadPresent()
    {
        var assembly = typeof(ResourceLocalizationIntegrationTests).Assembly;
        var assemblyDirectory = Path.GetDirectoryName(assembly.Location) ?? string.Empty;
        var priPath = Path.Combine(assemblyDirectory, "DroidNet.Resources.Tests.pri");
        _ = File.Exists(priPath).Should().BeTrue("Test PRI payload must be deployed next to the test assembly.");
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

    private static void ResetResourceExtensionsProvider()
    {
        var providerField = typeof(ResourceExtensions).GetField("provider", BindingFlags.NonPublic | BindingFlags.Static);
        providerField?.SetValue(null, null);
    }
}
