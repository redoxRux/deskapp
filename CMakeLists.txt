cmake_minimum_required(VERSION 3.10)
project(deskapp)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

set(IMGUI_DIR /Users/adityahebbar/programs/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Add tinyfiledialogs.c to the sources
add_executable(deskapp 
    main.cpp 
    tinyfiledialogs.c 
    stb_image_impl.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(deskapp PRIVATE 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}  # Add this to include the current directory
)

target_link_libraries(deskapp PRIVATE 
    glfw 
    ${OPENGL_LIBRARIES}
    "-framework Cocoa" 
    "-framework IOKit" 
    "-framework CoreVideo"
)

# Ensure tinyfiledialogs.c is compiled as C
set_source_files_properties(tinyfiledialogs.c PROPERTIES LANGUAGE C)

# Set the MACOSX_RPATH property
set_target_properties(deskapp PROPERTIES MACOSX_RPATH ON)