#include "EnchInfo.h"

bool EnchInfo::operator==(const EnchInfo& other) const {
    return name_id == other.name_id;
}
