#include "libqtest.h"
#include "libqos/pci-pc.h"

#include "hw/pci_regs.h"

#include "qemu-common.h"
#include "bitops.h"

#include <glib.h>

typedef struct QPCIBusPC
{
    QPCIBus bus;

    uint32_t pci_hole_start;
    uint32_t pci_hole_size;
    uint32_t pci_hole_alloc;

    uint16_t pci_iohole_start;
    uint16_t pci_iohole_size;
    uint16_t pci_iohole_alloc;
} QPCIBusPC;

static uint8_t qpci_pc_io_readb(QPCIBus *bus, void *addr)
{
    uintptr_t port = (uintptr_t)addr;
    uint8_t value;

    if (port < 0x10000) {
        value = inb(port);
    } else {
        memread(port, &value, sizeof(value));
    }

    return value;
}

static uint16_t qpci_pc_io_readw(QPCIBus *bus, void *addr)
{
    uintptr_t port = (uintptr_t)addr;
    uint16_t value;

    if (port < 0x10000) {
        value = inw(port);
    } else {
        memread(port, &value, sizeof(value));
    }

    return value;
}

static uint32_t qpci_pc_io_readl(QPCIBus *bus, void *addr)
{
    uintptr_t port = (uintptr_t)addr;
    uint32_t value;

    if (port < 0x10000) {
        value = inl(port);
    } else {
        memread(port, &value, sizeof(value));
    }

    return value;
}

static void qpci_pc_io_writeb(QPCIBus *bus, void *addr, uint8_t value)
{
    uintptr_t port = (uintptr_t)addr;

    if (port < 0x10000) {
        outb(port, value);
    } else {
        memwrite(port, &value, sizeof(value));
    }
}

static void qpci_pc_io_writew(QPCIBus *bus, void *addr, uint16_t value)
{
    uintptr_t port = (uintptr_t)addr;

    if (port < 0x10000) {
        outw(port, value);
    } else {
        memwrite(port, &value, sizeof(value));
    }
}

static void qpci_pc_io_writel(QPCIBus *bus, void *addr, uint32_t value)
{
    uintptr_t port = (uintptr_t)addr;

    if (port < 0x10000) {
        outl(port, value);
    } else {
        memwrite(port, &value, sizeof(value));
    }
}

static uint8_t qpci_pc_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    return inb(0xcfc);
}

static uint16_t qpci_pc_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    return inw(0xcfc);
}

static uint32_t qpci_pc_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    return inl(0xcfc);
}

static void qpci_pc_config_writeb(QPCIBus *bus, int devfn, uint8_t offset, uint8_t value)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    outb(0xcfc, value);
}

static void qpci_pc_config_writew(QPCIBus *bus, int devfn, uint8_t offset, uint16_t value)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    outw(0xcfc, value);
}

static void qpci_pc_config_writel(QPCIBus *bus, int devfn, uint8_t offset, uint32_t value)
{
    outl(0xcf8, (1 << 31) | (devfn << 8) | offset);
    outl(0xcfc, value);
}

static void *qpci_pc_iomap(QPCIBus *bus, QPCIDevice *dev, int barno)
{
    QPCIBusPC *s = container_of(bus, QPCIBusPC, bus);
    static const int bar_reg_map[] = {
        PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
    };
    int bar_reg;
    uint32_t addr;
    uint64_t size;

    g_assert(barno >= 0 && barno <= 5);
    bar_reg = bar_reg_map[barno];

    qpci_config_writel(dev, bar_reg, 0xFFFFFFFF);
    addr = qpci_config_readl(dev, bar_reg);

    size = (1ULL << ffz(addr));
    if (size == 0) {
        return NULL;
    }

    if (addr & PCI_BASE_ADDRESS_SPACE_IO) {
        uint16_t loc;

        g_assert((s->pci_iohole_alloc + size) <= s->pci_iohole_size);
        loc = s->pci_iohole_start + s->pci_iohole_alloc;
        s->pci_iohole_alloc += size;

        qpci_config_writel(dev, bar_reg, loc | PCI_BASE_ADDRESS_SPACE_IO);

        return (void *)(intptr_t)loc;
    } else {
        uint64_t loc;

        g_assert((s->pci_hole_alloc + size) <= s->pci_hole_size);
        loc = s->pci_hole_start + s->pci_hole_alloc;
        s->pci_hole_alloc += size;

        qpci_config_writel(dev, bar_reg, loc);

        return (void *)(intptr_t)loc;
    }
}

static void qpci_pc_iounmap(QPCIBus *bus, void *data)
{
    /* FIXME */
}

QPCIBus *qpci_init_pc(void)
{
    QPCIBusPC *ret;

    ret = g_malloc(sizeof(*ret));

    ret->bus.io_readb = qpci_pc_io_readb;
    ret->bus.io_readw = qpci_pc_io_readw;
    ret->bus.io_readl = qpci_pc_io_readl;

    ret->bus.io_writeb = qpci_pc_io_writeb;
    ret->bus.io_writew = qpci_pc_io_writew;
    ret->bus.io_writel = qpci_pc_io_writel;

    ret->bus.config_readb = qpci_pc_config_readb;
    ret->bus.config_readw = qpci_pc_config_readw;
    ret->bus.config_readl = qpci_pc_config_readl;

    ret->bus.config_writeb = qpci_pc_config_writeb;
    ret->bus.config_writew = qpci_pc_config_writew;
    ret->bus.config_writel = qpci_pc_config_writel;

    ret->bus.iomap = qpci_pc_iomap;
    ret->bus.iounmap = qpci_pc_iounmap;

    ret->pci_hole_start = 0xE0000000;
    ret->pci_hole_size = 0x20000000;
    ret->pci_hole_alloc = 0;

    ret->pci_iohole_start = 0xc000;
    ret->pci_iohole_size = 0x4000;
    ret->pci_iohole_alloc = 0;

    return &ret->bus;
}