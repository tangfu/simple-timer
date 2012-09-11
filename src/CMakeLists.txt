aux_source_directory(. SRC_LIST)
add_library(simpletimer ${SRC_LIST})

include(FindDoxygen)

add_custom_command(TARGET config_parse
    POST_BUILD
    COMMENT "[ generate doc ]"
    COMMAND ${DOXYGEN_EXECUTABLE} 2>/dev/null
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    #VERBATIM
    )

add_custom_command(TARGET config_parse
    POST_BUILD
    COMMENT "[ astyle code ]"
    COMMAND astyle src/*.c src/*.h 1>/dev/null 2>/dev/null
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    #VERBATIM
    )
