function(slark_collect_sources out_variable root_directory)
    file(GLOB_RECURSE collected_sources CONFIGURE_DEPENDS
        "${root_directory}/*.cpp"
        "${root_directory}/*.hpp"
        "${root_directory}/*.h"
        "${root_directory}/*.mm"
        "${root_directory}/*.m"
    )
    set(${out_variable} ${collected_sources} PARENT_SCOPE)
endfunction()
