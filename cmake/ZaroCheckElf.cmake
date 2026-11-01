# cmake -DREADELF=<llvm-readelf> -DLIB=<lib.so> -P ZaroCheckElf.cmake
#
# Checks a shared library before it goes into an APK or App Bundle:
# - No NEEDED entry with a '/'. A prebuilt library without SONAME that is linked by path records
#   this machine's path, and the device's dynamic linker then cannot load the app (D-022).
# - Every LOAD segment aligned to at least 16 KB (0x4000). Google Play rejects updates whose
#   native libraries do not support 16 KB pages from 2027-02-01 (D-025).
execute_process(COMMAND ${READELF} -dlW ${LIB} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zaro: ${READELF} -dlW ${LIB} failed")
endif()
string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*\\[[^]\n]*/[^]\n]*\\]" bad "${out}")
if(bad)
  message(FATAL_ERROR "zaro: ${LIB} depends on a build-machine path: ${bad}")
endif()
string(REGEX MATCHALL "\n *LOAD [^\n]* 0x[0-9a-f]+" loads "${out}")
if(NOT loads)
  message(FATAL_ERROR "zaro: ${LIB} has no LOAD segments in readelf output")
endif()
foreach(load ${loads})
  string(REGEX MATCH "0x[0-9a-f]+$" align "${load}")
  math(EXPR align_value "${align}")
  if(align_value LESS 16384)
    message(FATAL_ERROR "zaro: ${LIB} has a LOAD segment aligned to ${align}, below 16 KB (0x4000);"
                        " link with -Wl,-z,max-page-size=16384 or use NDK r28 or later (D-025)")
  endif()
endforeach()
