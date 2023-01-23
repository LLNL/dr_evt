# Discrete Resource Event Modeling and Multi-cluster Scheduling Simulator
 Discrete resource event modeling and multi-cluster scheduling simulator
 (DR_EVT) aims to provide a computational environment for simulating job
 scheduling and resource management using a set of heterogenous clusters.
 Currently, we proivde means to load and process job trace files, and
 are working on to build a worklod model based on the trace data.
 Once we have a workload model, we will use it feed a mult-cluster
 simulator that will be developed.

## Current Requirements:
 + **Platforms targeted**: Linux-based systems
 + **c++ compiler that supports c++17**
   e.g., clang++ 5.0 or later, g++ 7.1 or later, and icpc 19.0.1 or later

 + **GNU Boost library**
   The particular boost libraries used are `multi_index`, `filesystem`, `regex` and
   `system`.
   To build with other pre-existing installation of boost, set the environment
   variable `BOOST_ROOT` or pass `-DBOOST_ROOT=<path-to-boost>`
   to cmake. An example path to boost on LC is
   `/usr/tce/packages/boost/boost-1.69.0-mvapich2-2.3-gcc-8.1.0`.
   To run the executable, add `${BOOST_ROOT}/lib` to the `LD_LIBRARY_PATH` as
   needed

 + **Guide specific to using clang on Livermore Computing (LC) platforms**
   Currently, to use clang on Livermore Computing (LC) platforms with
   the libraries compiled with gcc, make sure the c++ standard library
   is compatible. On LC, clang by default is paired with c++ standard
   library from gcc/4.9.3. To avoid incompatibility issue,

   0) Use the user libraries compiled with the system default compiler.
      On many of LC platforms, it is currently gcc/4.9.3. On others,
      it depends on how clang is configure there.
   1) Make clang use the c++ standard library from the same version of gcc
      as that used for building user libraries to link.
      e.g., clang++ --gcc-toolchain=/usr/tce/packages/gcc/gcc-8.1.0/ ...
   2) Use clang's c++ standard library. Recompile user libraries in the
      same way as needed.
      i.e., clang++ --stdlib=libc++ ...

   Choose either `USE_GCC_LIBCXX` for option 1 or `USE_CLANG_LIBCXX` for
   option 2 if needed. Usually, option 1 works best. Option 0 does not work
   well especially with Catch2 due to the incomplete support for C++17.
   If neither is chosen, the build relies on the system default, which is,
   on LC, with `USE_GCC_LIBCXX` on and `GCC_TOOLCHAIN_VER` set to "4.9.3".
   If both are on, `USE_GCC_LIBCXX` is turned off. When `USE_GCC_LIBCXX`
   is on, `GCC_TOOLCHAIN_VER` can be set accordingly (e.g., "8.1.0").
   To make sure to use the correct compiler, invoke the cmake command as
   `CC=clang CXX=clang++ cmake ...`.

 + **Guide specific to using the intel compiler on Livermore Computing (LC) platforms**

   For gcc Interoperability, use `-DGCC_PATH=<path-to-gcc>`. Otherwise,
   icc picks up the gcc in default path, which may not support c++17.
   Finally, invoke the cmake command as `CC=icc CXX=icpc cmake ...`.

 + **cmake 3.12 or later**

   This requirement mostly comes from the compatibility between the cmake
   module `find_package()`, and the version of boost used. An older version
   might still work with some warnings.

 + [**Cereal**](https://uscilab.github.io/cereal)

   We rely on Cereal serialization library to enable state packing and
   unpacking of which needs arises under various circumstances including
   messaging, rollback, migration, and checkpointing. Cereal is a c++
   header-only library. No pre-installation is required as it is
   automatically downloaded and made available.

 + [**Protocol Buffers**](https://developers.google.com/protocol-buffers)

   We use the google protocol buffers library for parsing the configuration file
   of simulation, which is written by users in the [**protocol buffers language**](https://developers.google.com/protocol-buffers/docs/proto3).
   This is a required package. A user can indicate the location of a
   pre-installed copy via `-DPROTOBUF_ROOT=<path>`. Without it, building DR_EVT
   consists of two stages. In the first stage, the source of protocol buffer will
   be downloaded. Then, the library as well as the protoc compiler will be built
   and installed under where the rest of DR_EVT project will be.
   In the second stage, the DR_EVT project will be built using the protocol buffer
   installed in the first stage. Both stages require the same set of options for
   the cmake command.
   In case of cross-compiling, the path to the protoc compiler and the path to
   the library built for the target platform can be explicitly specified via
   `-DProtobuf_PROTOC_EXECUTABLE=<installation-for-host/bin/protoc>`
   and `-DPROTOBUF_DIR=<installation-for-target>` respectively.


## Getting started:
 ```
 git clone https://github.com/llnl/dr_evt.git
 mkdir build; cd build
 # The first invocation of cmake will setup to build protocol buffer
 cmake -DBOOST_ROOT:PATH=<PathToYourBoostDev> \
       -DCMAKE_INSTALL_PREFIX:PATH=<YourInstallPath> \
       ../dr_evt
 make -j 4
 # The second invocation of cmake will setup to build the DR_EVT project
 cmake -DBOOST_ROOT:PATH=<PathToYourBoostDev> \
       -DCMAKE_INSTALL_PREFIX:PATH=<YourInstallPath> \
       ../dr_evt
 make -j 4
 make install
 ```

## Taking advanage of OpenMP:
 + Build DR_EVT with the cmake option `-DDR_EVT_WITH_OPENMP:BOOL=ON`.
 Then, for accelerating execution performance, use the OpenMP environment
 variables to control the parallelism and the processor affinity, such as
 `OMP_NUM_THREADS`, `OMP_PROC_BIND`, and `OMP_PLACES`.


## Unit testing:
 + [**Catch2**](https://github.com/catchorg/Catch2)
 We rely on Catch2 for unit testing. To enable testing for development, set the
 environment variable `DR_EVT_WITH_UNIT_TESTING` or invoke cmake with the
 option `-DDR_EVT_WITH_UNIT_TESTING=ON`. No pre-installation of Catch2 is
 required as it is automatically downloaded and made available.
 To use a pre-installed copy, use the variable `CATCH2_ROOT`.

## Authors:
  Many thanks go to DR_EVT's [contributors](https://github.com/llnl/dr_evt/graphs/contributors).

## Release:
 DR_EVT is distributed under the terms of the MIT license.
 All new contributions must be made under this license.
 See [LICENSE](https://github.com/llnl/wcs/blob/master/LICENSE) and [NOTICE](https://github.com/llnl/wcs/blob/master/NOTICE) for details.

 + `SPDX-License-Identifier: MIT`
 + `LLNL-CODE-844050`

## Contributing:
 Please submit any bugfixes or feature improvements as [pull requests](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork).
