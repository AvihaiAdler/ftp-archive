set(LL_UNIT_TESTS ll_sanity)

foreach(TEST ${LL_UNIT_TESTS})
  
add_executable(${TEST} ${TEST}.c)
add_test(NAME ${TEST} COMMAND ${PROJECT_SOURCE_DIR}/build/lib/generics/tests/linked_list/${TEST})

target_compile_options(${TEST} PRIVATE -Wall -Wextra -pedantic -O3 -fsanitize=address,undefined)
target_link_options(${TEST} PRIVATE -fsanitize=address,undefined)

target_include_directories(${TEST} PRIVATE ${CMAKE_SOURCE_DIR}/lib/generics)
target_link_libraries(${TEST} PRIVATE generics)

endforeach(TEST)