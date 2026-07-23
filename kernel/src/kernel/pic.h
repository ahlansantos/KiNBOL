#ifndef PIC_H
#define PIC_H

#include <stdint.h>
#include <stdbool.h>

void pic_disable(bool imcr_switch_to_apic);

#endif