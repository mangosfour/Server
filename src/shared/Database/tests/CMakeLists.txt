# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(sql_transaction_direct_result_test
    sql_transaction_direct_result_test.cpp)
target_link_libraries(sql_transaction_direct_result_test PRIVATE shared)
set_target_properties(sql_transaction_direct_result_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
add_test(NAME sql_transaction_direct_result
    COMMAND sql_transaction_direct_result_test)

add_executable(database_content_version_test
    database_content_version_test.cpp)
target_include_directories(database_content_version_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(database_content_version_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
add_test(NAME database_content_version
    COMMAND database_content_version_test)

if(WIN32)
    get_filename_component(SQL_TRANSACTION_MYSQL_RUNTIME_DIR
        "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(sql_transaction_direct_result PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${SQL_TRANSACTION_MYSQL_RUNTIME_DIR}")
endif()
