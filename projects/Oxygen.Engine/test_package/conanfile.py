"""Exercise Oxygen as an application dependency, outside the engine build."""
import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class OxygenConsumer(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        dependency = self.dependencies["oxygen"]
        toolchain = CMakeToolchain(self)
        toolchain.variables["OXYGEN_TEST_TARGETS"] = ";".join(
            info.get_property("cmake_target_name")
            for info in dependency.cpp_info.components.values()
        )
        checker = str(dependency.options.awaitable_state_checker)
        toolchain.variables["OXYGEN_TEST_CHECKER"] = (
            self.settings.build_type == "Debug" if checker == "auto" else checker == "True")
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            executable = os.path.join(self.cpp.build.bindirs[0], "oxygen-consumer")
            self.run(f'"{executable}"', env="conanrun")
            if str(self.dependencies["oxygen"].options.modules) == "full":
                for tool in ("Oxygen.Cooker.ImportTool", "Oxygen.Cooker.PakTool",
                             "Oxygen.Cooker.PakDump", "Oxygen.Cooker.Inspector",
                             "Oxygen.Graphics.Direct3D12.ShaderBake", "Oxygen.Examples.RenderScene"):
                    self.run(f'{tool} --help', env="conanrun")
