# BestEnchSeq-Core

## Overview

A C++20 tool to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It's not only supports enchantments in Vanilla Minecraft, also those in vary mods, cause it is using extensible enchantment sheet to maintain enchantment infomation and can easily add third-party/custom enchantments.

### Key Function

- [ ] Calculate the mostly best enchanting order/forging sequence by your given needs
- [ ] Support inventory managemant, providing well handle of complex enchanted items/situation (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [ ] Support third-party/custom enchantments by editing custom enchantment sheet
- [ ] Support third-party/custom equipments by editing custom equipment sheet
- [ ] High performance hamming/enumeration algorithm for super fast calculating
%% - [ ] Support hot loading custom algorithm to feed special needs %%
- [ ] Easily export ro share calculation results with others
- [ ] Optionally hosting a Restful API service for external applications

### Quick Satrt

#### From binary distribution
#### From source code

## Core Types

### Common
#### MCE
#### EquipmentCategory

### Enchantment
#### EnchInfo
#### Ench
#### EnchSet

### Equipment
#### EquipmentType

### Item
#### ItemStack
#### ItemCollection

## Persistence

## Workflow

## Algorithm

## Interface

## Contributing

## License

> MIT License
