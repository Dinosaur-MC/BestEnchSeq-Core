#include "ItemStack.h"

bool ItemStack::is_book() const { return durability <= 0; }
bool ItemStack::is_equipment() const { return durability > 0; }

int32_t ItemStack::get_penalty_cost(int32_t n) { return (1 << n) - 1; }
int32_t ItemStack::get_penalty_cost() const { return (1 << prior_penalty) - 1; }
