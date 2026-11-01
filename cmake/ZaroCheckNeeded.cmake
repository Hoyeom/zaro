# cmake -DREADELF=<llvm-readelf> -DLIB=<lib.so> -P ZaroCheckNeeded.cmake
#
# Fails when a shared library records a build-machine path as a dependency (a NEEDED entry with a
# '/'). That happens when a prebuilt library without SONAME is linked by path, and the device's
# dynamic linker then cannot load the app (D-021).
execute_process(COMMAND ${READELF} -d ${LIB} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zaro: ${READELF} -d ${LIB} failed")
endif()
string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*\\[[^]\n]*/[^]\n]*\\]" bad "${out}")
if(bad)
  message(FATAL_ERROR "zaro: ${LIB} depends on a build-machine path: ${bad}")
endif()
