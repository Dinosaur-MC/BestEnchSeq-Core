# ── besq-domain-business ───────────────────────────────────────────────────
# Business (domain model) domain: user-facing game concept types and
# registries.  Depends on besq-common-core (public API), besq-common-io
# (JSON/CSV for loading), and optional log.

file(GLOB_RECURSE BESQ_BUSINESS_SRCS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(besq-domain-business STATIC ${BESQ_BUSINESS_SRCS})

target_link_libraries(besq-domain-business
    PUBLIC  besq-common-core          # NSID types in public API
    PRIVATE besq-common-io            # JSON / CSV for registries
    PRIVATE besq-common-log           # internal logging
)

target_include_directories(besq-domain-business PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
