/*
 * Copyright (c) 2026 Process Mission
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree interrupt numbers for the Raspberry Pi RP1 MSI-X
 *        interrupt controller.
 *
 * The RP1 exposes 61 MSI-X vectors. Each vector becomes one GIC SPI in
 * the BCM2712 MSI interrupt proxy (MIP), but Zephyr keeps them in a
 * separate, higher IRQ range so that both the GIC and the RP1 sources
 * fit in one flat interrupt space.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_RP1_IRQ_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_RP1_IRQ_H_

/*
 * BCM2712 wires GIC SPIs up to 277, i.e. Zephyr IRQ 309. Reserve the
 * range above that so a new SPI can never collide with an RP1 vector.
 */
#define RP1_IRQ_BASE  320
#define RP1_IRQ_LIMIT (RP1_IRQ_BASE + 61)

/** @brief Zephyr IRQ number of RP1 MSI-X vector @p n. */
#define RP1_IRQ(n) (RP1_IRQ_BASE + (n))

/* RP1 MSI-X vector numbers, see the RP1 datasheet. */
#define RP1_IRQ_IO_BANK0   RP1_IRQ(0)
#define RP1_IRQ_IO_BANK1   RP1_IRQ(1)
#define RP1_IRQ_IO_BANK2   RP1_IRQ(2)
#define RP1_IRQ_AUDIO_IN   RP1_IRQ(3)
#define RP1_IRQ_AUDIO_OUT  RP1_IRQ(4)
#define RP1_IRQ_PWM0       RP1_IRQ(5)
#define RP1_IRQ_ETH        RP1_IRQ(6)
#define RP1_IRQ_I2C0       RP1_IRQ(7)
#define RP1_IRQ_I2C1       RP1_IRQ(8)
#define RP1_IRQ_I2C2       RP1_IRQ(9)
#define RP1_IRQ_I2C3       RP1_IRQ(10)
#define RP1_IRQ_I2C4       RP1_IRQ(11)
#define RP1_IRQ_I2C5       RP1_IRQ(12)
#define RP1_IRQ_I2C6       RP1_IRQ(13)
#define RP1_IRQ_I2S0       RP1_IRQ(14)
#define RP1_IRQ_I2S1       RP1_IRQ(15)
#define RP1_IRQ_I2S2       RP1_IRQ(16)
#define RP1_IRQ_SDIO0      RP1_IRQ(17)
#define RP1_IRQ_SDIO1      RP1_IRQ(18)
#define RP1_IRQ_SPI0       RP1_IRQ(19)
#define RP1_IRQ_SPI1       RP1_IRQ(20)
#define RP1_IRQ_SPI2       RP1_IRQ(21)
#define RP1_IRQ_SPI3       RP1_IRQ(22)
#define RP1_IRQ_SPI4       RP1_IRQ(23)
#define RP1_IRQ_SPI5       RP1_IRQ(24)
#define RP1_IRQ_UART0      RP1_IRQ(25)
#define RP1_IRQ_TIMER0     RP1_IRQ(26)
#define RP1_IRQ_TIMER1     RP1_IRQ(27)
#define RP1_IRQ_TIMER2     RP1_IRQ(28)
#define RP1_IRQ_TIMER3     RP1_IRQ(29)
#define RP1_IRQ_USBHOST0   RP1_IRQ(30)
#define RP1_IRQ_USBHOST0_0 RP1_IRQ(31)
#define RP1_IRQ_USBHOST0_1 RP1_IRQ(32)
#define RP1_IRQ_USBHOST0_2 RP1_IRQ(33)
#define RP1_IRQ_USBHOST0_3 RP1_IRQ(34)
#define RP1_IRQ_USBHOST1   RP1_IRQ(35)
#define RP1_IRQ_USBHOST1_0 RP1_IRQ(36)
#define RP1_IRQ_USBHOST1_1 RP1_IRQ(37)
#define RP1_IRQ_USBHOST1_2 RP1_IRQ(38)
#define RP1_IRQ_USBHOST1_3 RP1_IRQ(39)
#define RP1_IRQ_DMA        RP1_IRQ(40)
#define RP1_IRQ_PWM1       RP1_IRQ(41)
#define RP1_IRQ_UART1      RP1_IRQ(42)
#define RP1_IRQ_UART2      RP1_IRQ(43)
#define RP1_IRQ_UART3      RP1_IRQ(44)
#define RP1_IRQ_UART4      RP1_IRQ(45)
#define RP1_IRQ_UART5      RP1_IRQ(46)
#define RP1_IRQ_MIPI0      RP1_IRQ(47)
#define RP1_IRQ_MIPI1      RP1_IRQ(48)
#define RP1_IRQ_VIDEO_OUT  RP1_IRQ(49)
#define RP1_IRQ_PIO0       RP1_IRQ(50)
#define RP1_IRQ_PIO1       RP1_IRQ(51)
#define RP1_IRQ_ADC_FIFO   RP1_IRQ(52)
#define RP1_IRQ_PCIE_OUT   RP1_IRQ(53)
#define RP1_IRQ_SPI6       RP1_IRQ(54)
#define RP1_IRQ_SPI7       RP1_IRQ(55)
#define RP1_IRQ_SPI8       RP1_IRQ(56)
#define RP1_IRQ_PROC_MISC  RP1_IRQ(57)
#define RP1_IRQ_SYSCFG     RP1_IRQ(58)
#define RP1_IRQ_CLOCKS     RP1_IRQ(59)
#define RP1_IRQ_VBUSCTRL   RP1_IRQ(60)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_RP1_IRQ_H_ */
