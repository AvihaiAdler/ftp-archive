include(CTest)

add_subdirectory(tests)

add_library(string src/str.c)

set_target_properties(
  string PROPERTIES 
  VERSION ${PROJECT_VERSION}
  PUBLIC_HEADER include/str.h
)

target_include_directories(string PRIVATE .)