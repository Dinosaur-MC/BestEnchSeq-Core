#include "registries/PlatformConfig.h"
// The static local in get_instance() is instantiated on first call.
// This TU ensures the singleton is emitted in a predictable translation unit.
