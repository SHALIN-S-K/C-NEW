/*
 * Copyright (c) 2026 Process Mission
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Raspberry Pi RP1 MSI-X interrupt controller driver API.
 *
 * The RP1 is a PCIe endpoint. It carries 61 MSI-X vectors, one per
 * internal interrupt source, and each vector is routed through the
 * BCM2712 MSI interrupt proxy (MIP) to a GIC SPI. The Zephyr IRQ space
 * keeps the RP1 vectors above the GIC SPIs, see
 * <zephyr/dt-bindings/interrupt-controller/rp1-irq.h>.
 *
 * The register-level building blocks live in this header behind a
 * small IO interface, so that the masking and acknowledgment semantics
 * can be exercised without the hardware. The driver in
 * drivers/interrupt_controller/intc_rp1.c owns the PCIe/MSI-X
 * discovery, the MIP programming and the MMIO implementation.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RP1_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RP1_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/dt-bindings/interrupt-controller/rp1-irq.h>
#include <zephyr/sys/util.h>

/**
 * @defgroup intc_rp1 RP1 MSI-X interrupt controller
 * @ingroup io_interfaces
 * @{
 */

/** @brief Offset in bytes of the MSI-X CFG block inside the RP1 PCIe APBS window. */
#define RP1_MSIX_APBS_OFFSET 0x108000U

/** @brief MSI-X CFG register block for direct read and write access. */
#define RP1_MSIX_CFG_RW  0x000U
/** @brief MSI-X CFG register block that sets the selected bits. */
#define RP1_MSIX_CFG_SET 0x800U
/** @brief MSI-X CFG register block that clears the selected bits. */
#define RP1_MSIX_CFG_CLR 0xc00U

/**
 * @brief Get the MSI-X CFG register offset of vector @p n.
 *
 * The vector number must be in the range 0 to 60.
 *
 * @return Register offset relative to the MSI-X CFG block base.
 */
#define RP1_MSIX_CFG(n) (0x8U + 4U * (n))

/** @brief Enable MSI generation for the selected vector. */
#define RP1_MSIX_CFG_ENABLE  BIT(0)
/** @brief Raise a test MSI for the selected vector. */
#define RP1_MSIX_CFG_TEST    BIT(1)
/** @brief Acknowledge the asserted MSI of the selected vector. */
#define RP1_MSIX_CFG_IACK    BIT(2)
/** @brief Keep the MSI asserted until the handler acknowledges it. */
#define RP1_MSIX_CFG_IACK_EN BIT(3)

/**
 * @brief Check whether an IRQ number is an RP1 vector.
 *
 * @p irq is an RP1 vector when it lies in the range RP1_IRQ_BASE to
 * RP1_IRQ_LIMIT - 1.
 */
#define RP1_IRQ_IS_RP1(irq) ((irq) >= RP1_IRQ_BASE && (irq) < RP1_IRQ_LIMIT)

/**
 * @brief Zephyr IRQ offset of the first GIC SPI.
 *
 * Zephyr numbers a GIC SPI as SPI + 32 because the first 32 INTIDs are
 * SGIs and PPIs, while the MIP properties carry hardware SPI numbers.
 */
#define RP1_GIC_SPI_IRQ_BASE 32U

/**
 * @brief Register access backend.
 *
 * The driver supplies an MMIO implementation; tests supply an in-memory
 * implementation.
 */
struct rp1_intc_io {
	/**
	 * @brief Write a 32-bit register.
	 *
	 * @param ctx Backend context passed to the callback.
	 * @param off Offset in bytes from the MSI-X CFG block base.
	 * @param val Value to write.
	 */
	void (*write)(void *ctx, unsigned int off, uint32_t val);

	/**
	 * @brief Read a 32-bit register.
	 *
	 * @param ctx Backend context passed to the callback.
	 * @param off Offset in bytes from the MSI-X CFG block base.
	 *
	 * @return The 32-bit value read.
	 */
	uint32_t (*read)(void *ctx, unsigned int off);
};

/** @brief RP1 controller state shared with the SoC IRQ glue. */
struct rp1_intc {
	/** @brief Register access backend. */
	const struct rp1_intc_io *io;
	/** @brief Context passed to the register access backend. */
	void *io_ctx;
	/** @brief First GIC SPI (hardware SPI number) used by the MIP. */
	uint32_t msi_base_spi;
	/** @brief Offset added to a vector number to obtain the MIP vector. */
	uint32_t msi_offset;
	/** @brief Number of MSI-X vectors managed by this controller, from 1 to 61. */
	uint32_t num_irqs;
	/** @brief Set when the PCIe and MIP setup completed. */
	bool ready;
};

/**
 * @brief Read a 32-bit register from the controller.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param off Offset in bytes from the MSI-X CFG block base.
 *
 * @return The 32-bit value read.
 */
static inline uint32_t rp1_intc_read(const struct rp1_intc *intc, unsigned int off)
{
	return intc->io->read(intc->io_ctx, off);
}

/**
 * @brief Write a 32-bit register to the controller.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param off Offset in bytes from the MSI-X CFG block base.
 * @param val Value to write.
 */
static inline void rp1_intc_write(const struct rp1_intc *intc, unsigned int off, uint32_t val)
{
	intc->io->write(intc->io_ctx, off, val);
}

/**
 * @brief Configure a controller instance and mask every vector.
 *
 * @param intc Controller instance to configure. Must not be NULL.
 * @param io Register access backend. Must remain valid while the
 *           controller is in use.
 * @param io_ctx Context passed to the backend callbacks.
 * @param msi_base_spi First GIC SPI (hardware SPI number) used by the MIP.
 * @param msi_offset Offset added to a vector number to obtain the MIP vector.
 * @param num_irqs Number of MSI-X vectors to manage, from 1 to 61.
 */
static inline void rp1_intc_setup(struct rp1_intc *intc, const struct rp1_intc_io *io, void *io_ctx,
				  uint32_t msi_base_spi, uint32_t msi_offset, uint32_t num_irqs)
{
	intc->io = io;
	intc->io_ctx = io_ctx;
	intc->msi_base_spi = msi_base_spi;
	intc->msi_offset = msi_offset;
	intc->num_irqs = num_irqs;

	/*
	 * The RP1 resets with every vector disabled. Mark every vector as
	 * level-triggered so that its MSI stays asserted until the handler
	 * acknowledges it. All RP1 sources except the USB host controllers
	 * are level-triggered; the USB drivers do not exist in Zephyr yet.
	 */
	for (uint32_t n = 0; n < num_irqs; n++) {
		rp1_intc_write(intc, RP1_MSIX_CFG_CLR + RP1_MSIX_CFG(n),
			       RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_IACK_EN);
		rp1_intc_write(intc, RP1_MSIX_CFG_SET + RP1_MSIX_CFG(n), RP1_MSIX_CFG_IACK_EN);
	}

	intc->ready = true;
}

/**
 * @brief Enable MSI generation for a vector.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param hwirq RP1 MSI-X vector number, from 0 to num_irqs - 1.
 */
static inline void rp1_intc_enable(struct rp1_intc *intc, unsigned int hwirq)
{
	if (!intc->ready || hwirq >= intc->num_irqs) {
		return;
	}

	rp1_intc_write(intc, RP1_MSIX_CFG_SET + RP1_MSIX_CFG(hwirq), RP1_MSIX_CFG_ENABLE);
}

/**
 * @brief Disable MSI generation for a vector.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param hwirq RP1 MSI-X vector number, from 0 to num_irqs - 1.
 */
static inline void rp1_intc_disable(struct rp1_intc *intc, unsigned int hwirq)
{
	if (!intc->ready || hwirq >= intc->num_irqs) {
		return;
	}

	rp1_intc_write(intc, RP1_MSIX_CFG_CLR + RP1_MSIX_CFG(hwirq), RP1_MSIX_CFG_ENABLE);
}

/**
 * @brief Check whether MSI generation is enabled for a vector.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param hwirq RP1 MSI-X vector number, from 0 to num_irqs - 1.
 *
 * @retval true MSI generation is enabled.
 * @retval false MSI generation is disabled, or the vector is out of range.
 */
static inline bool rp1_intc_is_enabled(const struct rp1_intc *intc, unsigned int hwirq)
{
	if (!intc->ready || hwirq >= intc->num_irqs) {
		return false;
	}

	return (rp1_intc_read(intc, RP1_MSIX_CFG(hwirq)) & RP1_MSIX_CFG_ENABLE) != 0U;
}

/**
 * @brief Acknowledge a vector after its handler ran.
 *
 * Level-triggered vectors hold their MSI asserted until this write.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param hwirq RP1 MSI-X vector number, from 0 to num_irqs - 1.
 */
static inline void rp1_intc_ack(struct rp1_intc *intc, unsigned int hwirq)
{
	if (!intc->ready || hwirq >= intc->num_irqs) {
		return;
	}

	if ((rp1_intc_read(intc, RP1_MSIX_CFG(hwirq)) & RP1_MSIX_CFG_IACK_EN) != 0U) {
		rp1_intc_write(intc, RP1_MSIX_CFG_SET + RP1_MSIX_CFG(hwirq), RP1_MSIX_CFG_IACK);
	}
}

/**
 * @brief Get the GIC IRQ number used by a vector.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param hwirq RP1 MSI-X vector number.
 *
 * @return Zephyr IRQ number of the GIC SPI (SPI + 32) for the vector.
 */
static inline unsigned int rp1_intc_hwirq_to_gic_irq(const struct rp1_intc *intc,
						     unsigned int hwirq)
{
	return intc->msi_base_spi + intc->msi_offset + RP1_GIC_SPI_IRQ_BASE + hwirq;
}

/**
 * @brief Map a GIC IRQ number back to an RP1 IRQ number.
 *
 * @param intc Controller instance. Must not be NULL.
 * @param gic_irq Zephyr IRQ number of a GIC SPI.
 * @param irq Output for the Zephyr IRQ number of the RP1 vector. Set
 *            only when the function returns true.
 *
 * @retval true gic_irq is routed through this controller and irq is set.
 * @retval false gic_irq is not routed through this controller.
 */
static inline bool rp1_intc_gic_irq_to_irq(const struct rp1_intc *intc, unsigned int gic_irq,
					   unsigned int *irq)
{
	unsigned int first = intc->msi_base_spi + intc->msi_offset + RP1_GIC_SPI_IRQ_BASE;

	if (!intc->ready || gic_irq < first || gic_irq >= first + intc->num_irqs) {
		return false;
	}

	*irq = RP1_IRQ_BASE + (gic_irq - first);

	return true;
}

/**
 * @brief Convert an RP1 IRQ number to a vector number.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 *
 * @return RP1 MSI-X vector number.
 */
static inline unsigned int rp1_intc_irq_to_hwirq(unsigned int irq)
{
	return irq - RP1_IRQ_BASE;
}

/*
 * Entry points used by the SoC IRQ glue. They operate on the single
 * RP1 controller instance described by the devicetree.
 */

/**
 * @brief Enable an RP1 IRQ.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 */
void rp1_intc_irq_enable(unsigned int irq);

/**
 * @brief Disable an RP1 IRQ.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 */
void rp1_intc_irq_disable(unsigned int irq);

/**
 * @brief Check whether an RP1 IRQ is enabled.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 *
 * @retval 1 The IRQ is enabled.
 * @retval 0 The IRQ is disabled, or the controller is not ready.
 */
int rp1_intc_irq_is_enabled(unsigned int irq);

/**
 * @brief Acknowledge an RP1 IRQ after its handler ran.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 */
void rp1_intc_irq_eoi(unsigned int irq);

/**
 * @brief Check whether the PCIe and MIP setup completed.
 *
 * @retval true The controller can route interrupts.
 * @retval false Initialization did not finish or failed.
 */
bool rp1_intc_is_ready(void);

/**
 * @brief Get the GIC IRQ number used by an RP1 IRQ.
 *
 * @param irq Zephyr IRQ number, from RP1_IRQ_BASE to RP1_IRQ_LIMIT - 1.
 *
 * @return Zephyr IRQ number of the GIC SPI (SPI + 32).
 */
unsigned int rp1_intc_gic_irq_from_irq(unsigned int irq);

/**
 * @brief Translate a GIC IRQ number to an RP1 IRQ number.
 *
 * @param gic_irq Zephyr IRQ number of a GIC SPI.
 * @param irq Output for the Zephyr IRQ number of the RP1 vector. Set
 *            only when the function returns true.
 *
 * @retval true gic_irq belongs to the RP1 controller and irq is set.
 * @retval false gic_irq is not routed through the RP1 controller.
 */
bool rp1_intc_irq_from_gic_irq(unsigned int gic_irq, unsigned int *irq);

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RP1_H_ */
