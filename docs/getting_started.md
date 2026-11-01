# Getting started

libaudition is a C++17 library. Its default build is designed to stay independent of ROS 2, Qt, microphone hardware and optional ML models.

## Build and test the library

From a checkout of the repository:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLIBAUDITION_BUILD_QT_DEMO=OFF \
  -DLIBAUDITION_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

Depending on local dependency availability, CMake may fetch missing source dependencies. Use the CMake option below if dependencies must already be installed:

~~~sh
cmake -S . -B build -DLIBAUDITION_FETCH_DEPENDENCIES=OFF
~~~

## Use the installed C++ package

After building and installing to a chosen prefix:

~~~sh
cmake --install build --prefix "$HOME/.local"
~~~

A downstream CMake target can link to exported components:

~~~cmake
find_package(libaudition CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE
  audition::core
  audition::dsp
  audition::spatial
  audition::memory
  audition::pipeline)
~~~

In C++:

~~~cpp
#include <audition/audition.hpp>
~~~

Configure a consuming project with the appropriate CMake prefix if necessary, for example with CMAKE_PREFIX_PATH. This illustrates package discovery; exact component selection depends on the algorithms used.

## Choose the next step

- [Audio signal flow and algorithms](functional_architecture.md) explains which processing blocks can be composed.
- [CLI examples](cli_examples.md) demonstrate library algorithms without a GUI.
- [Qt playground](qt_demo.md) and [full-demo preset](full_demo_build.md) cover optional interactive exploration.
- [Backend contracts](backend_contracts.md) explain adapter boundaries.
- [C++ API reference](api.rst) contains generated public declarations.
- [Tests and coverage](ctest_suites.md) explains available verification.

**Important:** Model files, licenses and compatible hardware remain separate requirements. A successful build is not a validation of real-array separation, speech accuracy, or real-time performance.
