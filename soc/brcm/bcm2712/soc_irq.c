/*
 * Copyright (c) 2026 Process Mission
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BCM2712 SoC IRQ glue. The GIC is the root interrupt controller, but
 * the RP1 PCIe south bridge delivers its interrupts through the MSI
 * interrupt proxy (MIP), which raises GIC SPIs. The RP1 sources are
 * kept in their own Zephyr IRQ range, above the GIC SPIs, so with
 * CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER the arch hooks below dispatch
 * each IRQ number to the GIC or to the RP1 controller.
 *
 * The hooks are also used before driver model initialisation finishes,
 * so every range check must tolerate the RP1 controller not being
 * ready yet.
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/interrupt-controller/arm-gic.h>
#include <zephyr/drivers/interrupt_controller/gic.h>
#include <zephyr/drivers/interrupt_controller/intc_rp1.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

/* GICv2 interrupt ID field of GICC_IAR. */
#define GIC_INTID_MASK 0x3ffU

void z_soc_irq_init(void)
{
	/*
	 * The GIC is initialised by its own PRE_KERNEL_1 device, and the
	 * RP1 controller by a later PRE_KERNEL_1 device that needs the
	 * PCIe root complex. Nothing to do at arch init time.
	 */
}

void z_soc_irq_enable(unsigned int irq)
{
	if (RP1_IRQ_IS_RP1(irq)) {
		if (!rp1_intc_is_ready()) {
			return;
		}

		rp1_intc_irq_enable(irq);
		arm_gic_irq_enable(rp1_intc_gic_irq_from_irq(irq));
		return;
	}

	arm_gic_irq_enable(irq);
}

void z_soc_irq_disable(unsigned int irq)
{
	if (RP1_IRQ_IS_RP1(irq)) {
		if (!rp1_intc_is_ready()) {
			return;
		}

		rp1_intc_irq_disable(irq);
		arm_gic_irq_disable(rp1_intc_gic_irq_from_irq(irq));
		return;
	}

	arm_gic_irq_disable(irq);
}

int z_soc_irq_is_enabled(unsigned int irq)
{
	if (RP1_IRQ_IS_RP1(irq)) {
		return rp1_intc_irq_is_enabled(irq);
	}

	return arm_gic_irq_is_enabled(irq);
}

void z_soc_irq_priority_set(unsigned int irq, unsigned int prio, unsigned int flags)
{
	if (RP1_IRQ_IS_RP1(irq)) {
		if (!rp1_intc_is_ready()) {
			return;
		}

		/*
		 * The RP1 has no per-source priority. The MIP raises an
		 * edge-triggered SPI, regardless of the source flags.
		 */
		arm_gic_irq_set_priority(rp1_intc_gic_irq_from_irq(irq), prio, IRQ_TYPE_EDGE);
		return;
	}

	arm_gic_irq_set_priority(irq, prio, flags);
}

unsigned int z_soc_irq_get_active(void)
{
	unsigned int iar = arm_gic_get_active();
	unsigned int irq;

	if (rp1_intc_irq_from_gic_irq(iar & GIC_INTID_MASK, &irq)) {
		return irq;
	}

	/* Preserve the full IAR value for the matching GIC EOI write. */
	return iar;
}

void z_soc_irq_eoi(unsigned int irq)
{
	if (RP1_IRQ_IS_RP1(irq)) {
		/*
		 * Release the RP1 level source before the GIC EOI, so that
		 * a still-asserted source cannot re-trigger the SPI.
		 */
		rp1_intc_irq_eoi(irq);
		arm_gic_eoi(rp1_intc_gic_irq_from_irq(irq));
		return;
	}

	arm_gic_eoi(irq);
}
