#include "Equipment.h"

bool Equipment::operator==(const Equipment& other) const { return name_id == other.name_id; }
