include(CTest)
add_subdirectory(tests/vector)
add_subdirectory(tests/linked_list)
add_subdirectory(tests/hash_table)
add_subdirectory(tests/string)

add_library(
  generics
  src/vector.c
  src/vector_s.c
  src/list.c
  src/hash_table.c
  src/str.c
)

set_target_properties(
  generics PROPERTIES 
  VERSION ${PROJECT_VERSION}
  PUBLIC_HEADER include/vector.h 
  PUBLIC_HEADER include/vector_s.h
  PUBLIC_HEADER include/list.h
  PUBLIC_HEADER include/hash_table.h
  PUBLIC_HEADER include/str.h
)
target_include_directories(generics PRIVATE .)