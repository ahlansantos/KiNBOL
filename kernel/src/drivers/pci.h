#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint32_t bar5;
} pci_device_t;

void pci_init(void);
void pci_enumerate(void);

uint32_t pci_read_dword_ext(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void     pci_enable_device(uint8_t bus, uint8_t slot, uint8_t func);

#endif