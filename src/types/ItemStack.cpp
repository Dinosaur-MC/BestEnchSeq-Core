#include "ItemStack.h"

int32_t ItemStack::get_penalty_cost(int32_t n) { return (1 << n) - 1; }
int32_t ItemStack::get_penalty_cost() const { return (1 << prior_penalty) - 1; }
