/*
 * Copyright (c) 2026 Process Mission
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Raspberry Pi RP1 MSI-X interrupt controller driver.
 *
 * The RP1 is the PCIe south bridge of the Raspberry Pi 5. Its 61
 * internal interrupt sources are delivered as individual MSI-X
 * vectors; the BCM2712 MSI interrupt proxy (MIP) turns each MSI write
 * into a GIC SPI. This driver programs the RP1 MSI-X table, unmasks the
 * host side of the MIP and exposes the RP1 sources in the Zephyr IRQ
 * space through the SoC IRQ glue.
 *
 * The MSI-X table lives in an RP1 BAR, so it is located through the
 * configuration space of the PCIe root complex that owns the RP1.
 * Because the MIP decodes a 64-bit MSI address, the generic Zephyr MSI
 * core (which hardcodes a zero upper address) cannot be reused.
 */

#define DT_DRV_COMPAT raspberrypi_rp1_irq

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/interrupt_controller/intc_rp1.h>
#include <zephyr/drivers/pcie/cap.h>
#include <zephyr/drivers/pcie/controller.h>
#include <zephyr/drivers/pcie/msi.h>
#include <zephyr/drivers/pcie/pcie.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(intc_rp1, CONFIG_INTC_LOG_LEVEL);

#define RP1_NODE  DT_INST_PARENT(0)
#define PCIE_NODE DT_PARENT(RP1_NODE)
#define MIP_NODE  DT_PHANDLE(DT_DRV_INST(0), msi_parent)

#define RP1_PCIE_VENDOR 0x1de4U
#define RP1_PCIE_DEVICE 0x0001U

/* The RP1 exposes 61 MSI-X vectors; the MIP may provide more SPIs. */
#define RP1_NUM_IRQS (RP1_IRQ_LIMIT - RP1_IRQ_BASE)

BUILD_ASSERT(DT_PROP(MIP_NODE, brcm_msi_num_spis) >= RP1_NUM_IRQS,
	     "MIP provides fewer SPIs than the RP1 has MSI-X vectors");

/* Configuration space word of the primary/secondary/subordinate bus numbers. */
#define PCI_CONF_BUS_NUM              6U
/* Configuration space word of the header type register. */
#define PCI_CONF_HEADER_TYPE          3U
#define PCI_HEADER_TYPE_MULTIFUNCTION BIT(7)

/* MIP registers. */
#define MIP_INT_CFGL_HOST  0x20U
#define MIP_INT_CFGH_HOST  0x30U
#define MIP_INT_MASKL_HOST 0x40U
#define MIP_INT_MASKH_HOST 0x50U
#define MIP_INT_MASKL_VPU  0x60U
#define MIP_INT_MASKH_VPU  0x70U

#define MIP_MSI_ADDR_HI DT_PROP_BY_IDX(MIP_NODE, brcm_msi_pci_addr, 0)
#define MIP_MSI_ADDR_LO DT_PROP_BY_IDX(MIP_NODE, brcm_msi_pci_addr, 1)

struct rp1_pcie_range {
	uint32_t flags;
	uint64_t pcie_bus_addr;
	uint64_t host_map_addr;
	uint64_t map_length;
};

#define RP1_PCIE_RANGE(node_id, idx)                                                               \
	{                                                                                          \
		.flags = DT_RANGES_CHILD_BUS_FLAGS_BY_IDX(node_id, idx),                           \
		.pcie_bus_addr = DT_RANGES_CHILD_BUS_ADDRESS_BY_IDX(node_id, idx),                 \
		.host_map_addr = DT_RANGES_PARENT_BUS_ADDRESS_BY_IDX(node_id, idx),                \
		.map_length = DT_RANGES_LENGTH_BY_IDX(node_id, idx),                               \
	},

static const struct rp1_pcie_range rp1_pcie_ranges[] = {
	DT_FOREACH_RANGE(PCIE_NODE, RP1_PCIE_RANGE)};

/*
 * Translate a PCIe bus address through the root complex ranges. Unlike
 * the generic region translate helper, this picks the window that
 * actually contains the address instead of trusting the BAR's 64-bit
 * flag, which does not say which window the firmware assigned.
 */
static bool rp1_translate_bus_addr(uint64_t bus_addr, uintptr_t *cpu_addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(rp1_pcie_ranges); i++) {
		const struct rp1_pcie_range *range = &rp1_pcie_ranges[i];
		uint32_t type = (range->flags >> 24) & 0x3U;

		if (type != 0x2U && type != 0x3U) {
			continue;
		}

		if (bus_addr >= range->pcie_bus_addr &&
		    bus_addr < range->pcie_bus_addr + range->map_length) {
			*cpu_addr = (uintptr_t)(range->host_map_addr +
						(bus_addr - range->pcie_bus_addr));
			return true;
		}
	}

	return false;
}

struct rp1_irq_config {
	DEVICE_MMIO_ROM;
	const struct device *pcie;
	uint32_t mip_base_spi;
	uint32_t mip_offset;
	uint32_t num_irqs;
};

struct rp1_irq_data {
	DEVICE_MMIO_RAM;
	struct rp1_intc intc;
	mm_reg_t mip;
};

static struct rp1_irq_data *rp1_irq_data(void)
{
	return DEVICE_DT_INST_GET(0)->data;
}

static void rp1_mmio_write(void *ctx, unsigned int off, uint32_t val)
{
	sys_write32(val, (mm_reg_t)(uintptr_t)ctx + off);
}

static uint32_t rp1_mmio_read(void *ctx, unsigned int off)
{
	return sys_read32((mm_reg_t)(uintptr_t)ctx + off);
}

static const struct rp1_intc_io rp1_mmio_io = {
	.write = rp1_mmio_write,
	.read = rp1_mmio_read,
};

static pcie_bdf_t rp1_find_bdf(const struct device *pcie)
{
	uint32_t buses;
	uint8_t bus;

	buses = pcie_ctrl_conf_read(pcie, PCIE_BDF(0, 0, 0), PCI_CONF_BUS_NUM);
	bus = (uint8_t)(buses >> 8);

	if (bus == 0U || bus == 0xffU) {
		LOG_ERR("PCIe root port has no secondary bus (0x%08x)", buses);
		return PCIE_BDF_NONE;
	}

	for (uint8_t dev = 0; dev < 32U; dev++) {
		for (uint8_t fn = 0; fn < 8U; fn++) {
			pcie_bdf_t bdf = PCIE_BDF(bus, dev, fn);
			uint32_t id = pcie_ctrl_conf_read(pcie, bdf, PCIE_CONF_ID);

			if (id == 0xffffffffU) {
				break;
			}

			if (id == PCIE_ID(RP1_PCIE_VENDOR, RP1_PCIE_DEVICE)) {
				return bdf;
			}

			if (fn == 0U) {
				uint32_t hdr = pcie_ctrl_conf_read(pcie, bdf, PCI_CONF_HEADER_TYPE);

				if ((hdr & (PCI_HEADER_TYPE_MULTIFUNCTION << 16)) == 0U) {
					break;
				}
			}
		}
	}

	LOG_ERR("RP1 not found on PCIe bus %u", bus);

	return PCIE_BDF_NONE;
}

static uint32_t rp1_find_msix_cap(const struct device *pcie, pcie_bdf_t bdf)
{
	uint32_t cmdstat = pcie_ctrl_conf_read(pcie, bdf, PCIE_CONF_CMDSTAT);
	uint32_t cap;

	if ((cmdstat & PCIE_CONF_CMDSTAT_CAPS) == 0U) {
		return 0U;
	}

	cap = PCIE_CONF_CAPPTR_FIRST(pcie_ctrl_conf_read(pcie, bdf, PCIE_CONF_CAPPTR));

	for (unsigned int i = 0; cap != 0U && i < 48U; i++) {
		uint32_t header = pcie_ctrl_conf_read(pcie, bdf, cap);

		if (PCIE_CONF_CAP_ID(header) == PCI_CAP_ID_MSIX) {
			return cap;
		}

		cap = PCIE_CONF_CAP_NEXT(header);
	}

	return 0U;
}

static bool rp1_read_bar(const struct device *pcie, pcie_bdf_t bdf, uint8_t idx,
			 uint64_t *bar_bus_addr)
{
	uint32_t bar = pcie_ctrl_conf_read(pcie, bdf, 4U + idx);
	uint32_t type = (bar >> 1) & 0x3U;

	if ((bar & 0x1U) != 0U || type == 0x1U) {
		return false;
	}

	*bar_bus_addr = bar & ~0xfULL;

	if (type == 0x2U) {
		if (idx >= 5U) {
			return false;
		}

		*bar_bus_addr |= (uint64_t)pcie_ctrl_conf_read(pcie, bdf, 5U + idx) << 32;
	}

	return true;
}

static int rp1_msix_setup(const struct device *pcie, pcie_bdf_t bdf, uint32_t num_vectors)
{
	const uint64_t msi_addr =
		((uint64_t)(uint32_t)MIP_MSI_ADDR_HI << 32) | (uint32_t)MIP_MSI_ADDR_LO;
	uint64_t bar_bus_addr;
	uintptr_t bar_addr;
	uint32_t cap, mcr, tr, table_size, count;
	uint8_t bir;
	uint8_t *table;

	cap = rp1_find_msix_cap(pcie, bdf);
	if (cap == 0U) {
		LOG_ERR("RP1 has no MSI-X capability");
		return -ENODEV;
	}

	mcr = pcie_ctrl_conf_read(pcie, bdf, cap + PCIE_MSIX_MCR);
	table_size = ((mcr & PCIE_MSIX_MCR_TSIZE) >> PCIE_MSIX_MCR_TSIZE_SHIFT) + 1U;
	count = MIN(table_size, num_vectors);

	if (table_size < num_vectors) {
		LOG_ERR("MSI-X table has %u vectors, expected %u", table_size, num_vectors);
		return -ENOTSUP;
	}

	tr = pcie_ctrl_conf_read(pcie, bdf, cap + PCIE_MSIX_TR);
	bir = tr & PCIE_MSIX_TR_BIR;
	if (bir > 5U) {
		return -EINVAL;
	}

	if (!rp1_read_bar(pcie, bdf, bir, &bar_bus_addr)) {
		LOG_ERR("MSI-X table BAR %u is not a memory BAR", bir);
		return -ENOTSUP;
	}

	if (!rp1_translate_bus_addr(bar_bus_addr, &bar_addr)) {
		LOG_ERR("MSI-X table BAR %u (0x%llx) is outside the PCIe windows", bir,
			(unsigned long long)bar_bus_addr);
		return -ENOTSUP;
	}

	k_mem_map_phys_bare(&table, bar_addr + (tr & PCIE_MSIX_TR_OFFSET),
			    count * PCIE_MSIR_TABLE_ENTRY_SIZE, K_MEM_CACHE_NONE | K_MEM_PERM_RW);

	for (uint32_t i = 0; i < count; i++) {
		uint8_t *entry = table + i * PCIE_MSIR_TABLE_ENTRY_SIZE;

		sys_write32((uint32_t)msi_addr, (mm_reg_t)(entry + PCIE_VTBL_MA));
		sys_write32((uint32_t)(msi_addr >> 32), (mm_reg_t)(entry + PCIE_VTBL_MUA));
		sys_write32(i, (mm_reg_t)(entry + PCIE_VTBL_MD));
		sys_write32(0U, (mm_reg_t)(entry + PCIE_VTBL_VCTRL));
	}

	LOG_DBG("RP1 MSI-X: %u vectors, table BAR %u offset 0x%x", table_size, bir,
		tr & PCIE_MSIX_TR_OFFSET);

	/* The vector enables are clear at this point, so unmask the function. */
	mcr &= ~PCIE_MSIX_MCR_FMASK;
	mcr |= PCIE_MSIX_MCR_EN;
	pcie_ctrl_conf_write(pcie, bdf, cap + PCIE_MSIX_MCR, mcr);

	return 0;
}

static void rp1_mip_setup(struct rp1_irq_data *data)
{
	/* Interrupts for the host, masked for the VPU, edge-triggered. */
	sys_write32(0U, data->mip + MIP_INT_MASKL_HOST);
	sys_write32(0U, data->mip + MIP_INT_MASKH_HOST);
	sys_write32(0xffffffffU, data->mip + MIP_INT_MASKL_VPU);
	sys_write32(0xffffffffU, data->mip + MIP_INT_MASKH_VPU);
	sys_write32(0xffffffffU, data->mip + MIP_INT_CFGL_HOST);
	sys_write32(0xffffffffU, data->mip + MIP_INT_CFGH_HOST);
}

static int rp1_irq_init(const struct device *dev)
{
	const struct rp1_irq_config *config = dev->config;
	struct rp1_irq_data *data = dev->data;
	pcie_bdf_t bdf;
	uint32_t cmdstat;
	int ret;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	device_map(&data->mip, DT_REG_ADDR(MIP_NODE), DT_REG_SIZE(MIP_NODE), K_MEM_CACHE_NONE);

	if (!device_is_ready(config->pcie)) {
		LOG_ERR("PCIe root complex is not ready");
		return -ENODEV;
	}

	rp1_mip_setup(data);

	bdf = rp1_find_bdf(config->pcie);
	if (bdf == PCIE_BDF_NONE) {
		return -ENODEV;
	}

	cmdstat = pcie_ctrl_conf_read(config->pcie, bdf, PCIE_CONF_CMDSTAT);
	pcie_ctrl_conf_write(config->pcie, bdf, PCIE_CONF_CMDSTAT,
			     cmdstat | PCIE_CONF_CMDSTAT_MEM | PCIE_CONF_CMDSTAT_MASTER);

	/* Keep every vector disabled while the MSI-X table is rewritten. */
	rp1_intc_setup(&data->intc, &rp1_mmio_io, (void *)(uintptr_t)DEVICE_MMIO_GET(dev),
		       config->mip_base_spi, config->mip_offset, config->num_irqs);

	ret = rp1_msix_setup(config->pcie, bdf, config->num_irqs);
	if (ret != 0) {
		data->intc.ready = false;
		return ret;
	}

	LOG_INF("RP1 IRQ controller ready: vectors %u..%u, SPIs %u..%u", RP1_IRQ_BASE,
		RP1_IRQ_BASE + config->num_irqs - 1U, config->mip_base_spi + config->mip_offset,
		config->mip_base_spi + config->mip_offset + config->num_irqs - 1U);

	return 0;
}

void rp1_intc_irq_enable(unsigned int irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	rp1_intc_enable(&data->intc, rp1_intc_irq_to_hwirq(irq));
}

void rp1_intc_irq_disable(unsigned int irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	rp1_intc_disable(&data->intc, rp1_intc_irq_to_hwirq(irq));
}

int rp1_intc_irq_is_enabled(unsigned int irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	return rp1_intc_is_enabled(&data->intc, rp1_intc_irq_to_hwirq(irq)) ? 1 : 0;
}

void rp1_intc_irq_eoi(unsigned int irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	rp1_intc_ack(&data->intc, rp1_intc_irq_to_hwirq(irq));
}

bool rp1_intc_is_ready(void)
{
	return rp1_irq_data()->intc.ready;
}

unsigned int rp1_intc_gic_irq_from_irq(unsigned int irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	return rp1_intc_hwirq_to_gic_irq(&data->intc, rp1_intc_irq_to_hwirq(irq));
}

bool rp1_intc_irq_from_gic_irq(unsigned int gic_irq, unsigned int *irq)
{
	struct rp1_irq_data *data = rp1_irq_data();

	return rp1_intc_gic_irq_to_irq(&data->intc, gic_irq, irq);
}

#define RP1_IRQ_INIT(n)                                                                            \
	static struct rp1_irq_data rp1_irq_data_##n;                                               \
	static const struct rp1_irq_config rp1_irq_config_##n = {                                  \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),                                              \
		.pcie = DEVICE_DT_GET(PCIE_NODE),                                                  \
		.mip_base_spi = DT_PROP(MIP_NODE, brcm_msi_base_spi),                              \
		.mip_offset = DT_PROP(MIP_NODE, brcm_msi_offset),                                  \
		.num_irqs = RP1_NUM_IRQS,                                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, rp1_irq_init, NULL, &rp1_irq_data_##n, &rp1_irq_config_##n,       \
			      PRE_KERNEL_1, CONFIG_RP1_IRQ_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(RP1_IRQ_INIT)
