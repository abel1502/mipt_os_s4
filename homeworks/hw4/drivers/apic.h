#pragma once

#include "acpi.h"

extern uint64_t ticks_per_sec;

// apic_init initializes APIC.
void apic_init();

// apic_eoi signals end-of-interrupt to the APIC. Must be called before interrupt handler finishes.
void apic_eoi();
