#include "pci.h"
#include "../kernel/dmesg.h"
#include "ahci.h"

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_get_vendor_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_read_dword(bus, slot, func, 0) & 0xFFFF);
}

static uint16_t pci_get_device_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_read_dword(bus, slot, func, 0) >> 16);
}

static uint8_t pci_get_class(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)(pci_read_dword(bus, slot, func, 8) >> 24);
}

static uint8_t pci_get_subclass(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)(pci_read_dword(bus, slot, func, 8) >> 16);
}

static uint8_t pci_get_prog_if(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)(pci_read_dword(bus, slot, func, 8) >> 8);
}

static uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_num) {
    return pci_read_dword(bus, slot, func, 0x10 + (bar_num * 4));
}

uint32_t pci_read_dword_ext(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_dword(bus, slot, func, offset);
}

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_read_dword(bus, slot, func, 0x04);
    cmd |= (1 << 1) | (1 << 2);
    pci_write_dword(bus, slot, func, 0x04, cmd);
}

void pci_check_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_get_vendor_id(bus, device, function);
    if (vendor_id == 0xFFFF) return;

    uint16_t device_id = pci_get_device_id(bus, device, function);
    uint8_t class_code = pci_get_class(bus, device, function);
    uint8_t subclass = pci_get_subclass(bus, device, function);
    uint8_t prog_if = pci_get_prog_if(bus, device, function);

    dmesg("[pci] found "); dmesg_hex(vendor_id); dmesg(":"); dmesg_hex(device_id);
    dmesg(" class="); dmesg_int(class_code);
    dmesg(" sub="); dmesg_int(subclass);
    dmesg(" prog_if="); dmesg_int(prog_if);
    dmesg("\n");

    if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
        dmesg("[pci] => AHCI Controller Found!\n");

        pci_enable_device(bus, device, function);
        dmesg("[pci] memory space + bus mastering enabled\n");

        pci_device_t ahci_dev;
        ahci_dev.vendor_id = vendor_id;
        ahci_dev.device_id = device_id;
        ahci_dev.bus = bus;
        ahci_dev.device = device;
        ahci_dev.function = function;
        ahci_dev.bar5 = pci_get_bar(bus, device, function, 5);

        ahci_init(&ahci_dev);
    }
}

void pci_enumerate(void) {
    dmesg("[pci] enumerating devices...\n");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_get_vendor_id((uint8_t)bus, slot, 0);
            if (vendor != 0xFFFF) {
                pci_check_device((uint8_t)bus, slot, 0);

                uint8_t header_type = (uint8_t)(pci_read_dword((uint8_t)bus, slot, 0, 0x0C) >> 16);
                if (header_type & 0x80) {
                    for (uint8_t func = 1; func < 8; func++) {
                        if (pci_get_vendor_id((uint8_t)bus, slot, func) != 0xFFFF) {
                            pci_check_device((uint8_t)bus, slot, func);
                        }
                    }
                }
            }
        }
    }
}

void pci_init(void) {
    pci_enumerate();
}