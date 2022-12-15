set(STR_SANITY str_sanity)

foreach(TEST ${STR_SANITY})
  add_executable(${TEST} ${TEST}.c)
  add_test(NAME ${TEST} COMMAND ${PROJECT_SOURCE_DIR}/build/lib/string/tests/${TEST})
  target_compile_options(${TEST} PRIVATE -Wall -Wextra -pedantic -O3 -fsanitize=address,undefined)
  target_link_options(${TEST} PRIVATE -fsanitize=address,undefined)

  target_include_directories(${TEST} PRIVATE ${CMAKE_SOURCE_DIR}/lib/string)
  target_link_libraries(${TEST} PRIVATE string)
endforeach(TEST)
