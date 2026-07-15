# data/CMakeLists.txt
#
# Embeds builtin data files into the binary at compile time.
# The root CMakeLists.txt calls add_subdirectory(data) to include this.

# Read vanilla.json as hex byte array
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/builtin/vanilla.json" _hex HEX)
string(LENGTH "${_hex}" _hex_len)
math(EXPR BESQ_EMBED_LEN "${_hex_len} / 2")

# Build byte-array initializer (16 bytes per line)
string(ASCII 10 _NL)
set(_bytes "")
set(_i 0)
set(_per_line 16)
while(_i LESS _hex_len)
    set(_line "")
    set(_sep "")
    set(_j 0)
    while(_j LESS _per_line AND _i LESS _hex_len)
        string(SUBSTRING "${_hex}" ${_i} 2 _byte)
        string(APPEND _line "${_sep}0x${_byte}")
        set(_sep ", ")
        math(EXPR _i "${_i} + 2")
        math(EXPR _j "${_j} + 1")
    endwhile()
    set(_bytes "${_bytes}    ${_line},${_NL}")
endwhile()

# Configure the C++ template → generated source file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/data/EmbeddedData.cpp.in"
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData.cpp"
    @ONLY
)

# Add generated source to the besq target (created in root CMakeLists.txt)
target_sources(${PROJECT_NAME} PRIVATE
    "${PROJECT_BINARY_DIR}/generated/EmbeddedData.cpp"
)
