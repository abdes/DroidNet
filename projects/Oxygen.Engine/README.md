# Oxygen Game Engine

## Install latest VC Redistributable Package

**Optimized version crashes on Mutex machinery in the STL.**

https://developercommunity.visualstudio.com/t/Visual-Studio-17100-Update-leads-to-Pr/10669759?sort=newest
I’m resolving it as By Design, as explained in our release notes:

Fixed mutex’s constructor to be constexpr.
Note: Programs that aren’t following the documented restrictions on binary compatibility may encounter null dereferences in mutex machinery. You must follow this rule:
When you mix binaries built by different supported versions of the toolset, the Redistributable version must be at least as new as the latest toolset used by any app component.

You can define _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR as an escape hatch.
That is, if you’re seeing crashes due to null dereferences in mutex locking machinery, you’re deploying a program built with new STL headers, but without a sufficiently new msvcp140.dll, which is unsupported. You need to be (re)distributing a new STL DLL too. (If a VS 2022 17.10 VCRedist has been independently installed on the machine - then everything will happen to work.)

Solve the problem based on Karel Van de Rostyne’s comment:

**The solution for this problem is:**
Download the latest Microsoft Visual C++ Redistributables and install them on
the machine that gives the problem.

On this Microsoft site you find the downloads.
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170

## Shader Compilation Setup

[text](https://github.com/Devaniti/GetDXC)
