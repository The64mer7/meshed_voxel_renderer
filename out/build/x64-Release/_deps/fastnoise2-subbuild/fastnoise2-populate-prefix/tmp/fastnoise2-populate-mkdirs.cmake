# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-build"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/tmp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/src/fastnoise2-populate-stamp"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/src"
  "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/src/fastnoise2-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/src/fastnoise2-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/jaros/Desktop/Programming/COpenglProject/meshed_voxel_renderer/out/build/x64-Release/_deps/fastnoise2-subbuild/fastnoise2-populate-prefix/src/fastnoise2-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
