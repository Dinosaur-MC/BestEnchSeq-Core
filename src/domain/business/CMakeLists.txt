# ── besq-domain-business ───────────────────────────────────────────────────
# Business (domain model) domain: user-facing game concept types and
# registries.  Depends only on besq-common.

file(GLOB_RECURSE BESQ_BUSINESS_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(besq-domain-business STATIC ${BESQ_BUSINESS_SRCS})

target_link_libraries(besq-domain-business PUBLIC besq-common)

target_include_directories(besq-domain-business PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
