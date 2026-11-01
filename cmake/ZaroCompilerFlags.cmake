# Compiler rules for every ZARO-owned target. Third-party targets do not get these.
#
# These flags are the mechanical half of the "restricted C++20" decision in
# docs/DECISIONS.md (D-001): exceptions and RTTI are off, so code that throws or
# uses dynamic_cast/typeid fails to compile. tools/lint.py covers what flags cannot.

function(zaro_target_defaults target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  # Unity builds only for ZARO code; third-party sources are not written for them.
  set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
      $<$<COMPILE_LANGUAGE:CXX>:/EHs-c- /GR->
      /fp:precise)
    target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Werror
      $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
      -ffp-contract=off)
  endif()
endfunction()
