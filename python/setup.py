#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Setup script for DR_EVT Python bindings

Builds the dr_evt Python module from C++ sources using pybind11.
"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import os
import subprocess


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        try:
            subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        cmake_args = [
            f'-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}',
            f'-DPython3_EXECUTABLE={sys.executable}',
            '-DDR_EVT_BUILD_PYTHON=ON',
        ]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg, '--parallel', '4']

        cmake_args += [f'-DCMAKE_BUILD_TYPE={cfg}']

        env = os.environ.copy()
        env['CXXFLAGS'] = f"{env.get('CXXFLAGS', '')} -DVERSION_INFO=\\'{self.distribution.get_version()}\\'"

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)


setup(
    name='dr_evt',
    version='0.9.0',
    author='LLNL',
    author_email='',
    description='DR_EVT: HPC Job Scheduler Simulator - Python API',
    long_description='''
Python bindings for DR_EVT HPC job scheduler simulator.

Provides:
- Streaming API for online simulation
- Monitoring API for resource and queue status
- Statistics API for performance metrics
''',
    ext_modules=[CMakeExtension('dr_evt', sourcedir=REPO_ROOT)],
    cmdclass=dict(build_ext=CMakeBuild),
    zip_safe=False,
    python_requires='>=3.7',
)
