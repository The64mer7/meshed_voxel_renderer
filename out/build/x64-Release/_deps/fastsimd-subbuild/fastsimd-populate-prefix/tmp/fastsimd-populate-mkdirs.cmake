# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-build"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/tmp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/src/fastsimd-populate-stamp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/src/fastsimd-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/src/fastsimd-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastsimd-subbuild/fastsimd-populate-prefix/src/fastsimd-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
