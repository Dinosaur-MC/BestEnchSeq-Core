# data/CMakeLists.txt
#
# Embeds builtin data files into the binary at compile time.
# Each embed_resource() call generates a .cpp with a hex byte array.
# Add new resources by calling embed_resource() + target_sources().

include(${CMAKE_SOURCE_DIR}/cmake/EmbedResource.cmake)

embed_resource(
    INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/builtin/vanilla.json"
    SYMBOL vanilla_json
    OUTPUT "${PROJECT_BINARY_DIR}/generated/EmbeddedData_vanilla.cpp"
)

embed_resource(
    INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/builtin/item_properties.json"
    SYMBOL item_properties
    OUTPUT "${PROJECT_BINARY_DIR}/generated/EmbeddedData_item_props.cpp"
)

target_sources(${PROJECT_NAME} PRIVATE
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData_vanilla.cpp"
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData_item_props.cpp"
)
