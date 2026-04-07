# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-build"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/tmp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/src/xsimd-populate-stamp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/src/xsimd-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/src/xsimd-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/xsimd-subbuild/xsimd-populate-prefix/src/xsimd-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
