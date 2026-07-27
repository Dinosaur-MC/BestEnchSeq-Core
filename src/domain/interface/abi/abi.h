#pragma once

/// @file domain/interface/abi/abi.h
/// Convenience shim: re-export the canonical public API header.
///
/// Consumers should prefer #include <besq/besq.h> directly.
/// This header exists so internal implementation files can include
/// it without depending on the public include directory layout.

#include "besq/besq.h" // IWYU pragma: export
