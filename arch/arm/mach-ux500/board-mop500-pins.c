/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/string.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_data/pinctrl-nomadik.h>

#include <asm/mach-types.h>

#include "board-mop500.h"

enum custom_pin_cfg_t {
	PINS_FOR_DEFAULT,
	PINS_FOR_U9500,
};

static enum custom_pin_cfg_t pinsfor;

/* These simply sets bias for pins */
#define BIAS(a,b) static unsigned long a[] = { b }

BIAS(pd, PIN_PULL_DOWN);
BIAS(in_nopull, PIN_INPUT_NOPULL);
BIAS(in_nopull_slpm_nowkup, PIN_INPUT_NOPULL|PIN_SLPM_WAKEUP_DISABLE);
BIAS(in_pu, PIN_INPUT_PULLUP);
BIAS(in_pd, PIN_INPUT_PULLDOWN);
BIAS(out_hi, PIN_OUTPUT_HIGH);
BIAS(out_lo, PIN_OUTPUT_LOW);
BIAS(out_lo_slpm_nowkup, PIN_OUTPUT_LOW|PIN_SLPM_WAKEUP_DISABLE);

BIAS(abx500_out_lo, PIN_CONF_PACKED(PIN_CONFIG_OUTPUT, 0));
BIAS(abx500_in_pd, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 1));
BIAS(abx500_in_nopull, PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 0));

/* These also force them into GPIO mode */
BIAS(gpio_in_pu, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pd, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pu_slpm_gpio_nopull, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_in_pd_slpm_gpio_nopull, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_out_hi, PIN_OUTPUT_HIGH|PIN_GPIOMODE_ENABLED);
BIAS(gpio_out_lo, PIN_OUTPUT_LOW|PIN_GPIOMODE_ENABLED);
/* Sleep modes */
BIAS(slpm_in_wkup_pdis, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_DIR_INPUT|PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_DISABLED);
BIAS(slpm_in_wkup_pdis_en, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_DIR_INPUT|PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_ENABLED);
BIAS(slpm_wkup_pdis, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_DISABLED);
BIAS(slpm_wkup_pdis_en, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_ENABLED);
BIAS(slpm_out_lo_pdis, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_OUTPUT_LOW|PIN_SLPM_WAKEUP_DISABLE|PIN_SLPM_PDIS_DISABLED);
BIAS(slpm_out_lo_wkup_pdis, PIN_SLEEPMODE_ENABLED|
	PIN_SLPM_OUTPUT_LOW|PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_DISABLED);
BIAS(slpm_out_hi_wkup_pdis, PIN_SLEEPMODE_ENABLED|PIN_SLPM_OUTPUT_HIGH|
	PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_DISABLED);
BIAS(slpm_in_pu_wkup_pdis_en, PIN_SLEEPMODE_ENABLED|PIN_SLPM_INPUT_PULLUP|
	PIN_SLPM_WAKEUP_ENABLE|PIN_SLPM_PDIS_ENABLED);

/* We use these to define hog settings that are always done on boot */
#define DB8500_MUX_HOG(group,func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-db8500", group, func)
#define DB8500_PIN_HOG(pin,conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-db8500", pin, conf)

/* These are default states associated with device and changed runtime */
#define DB8500_MUX(group,func,dev) \
	PIN_MAP_MUX_GROUP_DEFAULT(dev, "pinctrl-db8500", group, func)
#define DB8500_PIN(pin,conf,dev) \
	PIN_MAP_CONFIGS_PIN_DEFAULT(dev, "pinctrl-db8500", pin, conf)
#define DB8500_PIN_IDLE(pin, conf, dev) \
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_IDLE, "pinctrl-db8500",	\
			    pin, conf)
#define DB8500_PIN_SLEEP(pin, conf, dev) \
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_SLEEP, "pinctrl-db8500",	\
			    pin, conf)
#define DB8500_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-db8500", group, func)
#define DB8500_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-db8500", pin, conf)

#define AB8500_MUX_HOG(group, func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-ab8500.0", group, func)
#define AB8500_PIN_HOG(pin, conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-ab8500.0", pin, abx500_##conf)

#define AB8500_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-ab8500.0", group, func)
#define AB8500_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-ab8500.0", pin, abx500_##conf)

#define AB8505_MUX_HOG(group, func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-ab8505.0", group, func)
#define AB8505_PIN_HOG(pin, conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-ab8505.0", pin, abx500_##conf)

#define AB8505_MUX_STATE(group, func, dev, state) \
	PIN_MAP_MUX_GROUP(dev, state, "pinctrl-ab8505.0", group, func)
#define AB8505_PIN_STATE(pin, conf, dev, state) \
	PIN_MAP_CONFIGS_PIN(dev, state, "pinctrl-ab8505.0", pin, abx500_##conf)

static struct pinctrl_map __initdata ab8500_pinmap[] = {
	/* Sysclkreq2 */
	AB8500_MUX_STATE("sysclkreq2_d_1", "sysclkreq", "regulator.35", PINCTRL_STATE_DEFAULT),
	AB8500_PIN_STATE("GPIO1_T10", in_nopull, "regulator.35", PINCTRL_STATE_DEFAULT),
	/* sysclkreq2 disable, mux in gpio configured in input pulldown */
	AB8500_MUX_STATE("gpio1_a_1", "gpio", "regulator.35", PINCTRL_STATE_SLEEP),
	AB8500_PIN_STATE("GPIO1_T10", in_pd, "regulator.35", PINCTRL_STATE_SLEEP),

	/* pins 2 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8500_MUX_HOG("gpio2_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO2_T9", in_pd),

	/* Sysclkreq4 */
	AB8500_MUX_STATE("sysclkreq4_d_1", "sysclkreq", "regulator.36", PINCTRL_STATE_DEFAULT),
	AB8500_PIN_STATE("GPIO3_U9", in_nopull, "regulator.36", PINCTRL_STATE_DEFAULT),
	/* sysclkreq4 disable, mux in gpio configured in input pulldown */
	AB8500_MUX_STATE("gpio3_a_1", "gpio", "regulator.36", PINCTRL_STATE_SLEEP),
	AB8500_PIN_STATE("GPIO3_U9", in_pd, "regulator.36", PINCTRL_STATE_SLEEP),

	/* pins 4 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8500_MUX_HOG("gpio4_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO4_W2", in_pd),

	/*
	 * pins 6,7,8 and 9 are muxed in YCBCR0123
	 * configured in INPUT PULL UP
	 */
	AB8500_MUX_HOG("ycbcr0123_d_1", "ycbcr"),
	AB8500_PIN_HOG("GPIO6_Y18", in_nopull),
	AB8500_PIN_HOG("GPIO7_AA20", in_nopull),
	AB8500_PIN_HOG("GPIO8_W18", in_nopull),
	AB8500_PIN_HOG("GPIO9_AA19", in_nopull),

	/*
	 * pins 10,11,12 and 13 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio10_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO10_U17", in_pd),

	AB8500_MUX_HOG("gpio11_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO11_AA18", in_pd),

	AB8500_MUX_HOG("gpio12_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO12_U16", in_pd),

	AB8500_MUX_HOG("gpio13_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO13_W17", in_pd),

	/*
	 * pins 14,15 are muxed in PWM1 and PWM2
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("pwmout1_d_1", "pwmout"),
	AB8500_PIN_HOG("GPIO14_F14", in_pd),

	AB8500_MUX_HOG("pwmout2_d_1", "pwmout"),
	AB8500_PIN_HOG("GPIO15_B17", in_pd),

	/*
	 * pins 16 is muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio16_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO14_F14", in_pd),

	/*
	 * pins 17,18,19 and 20 are muxed in AUDIO interface 1
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("adi1_d_1", "adi1"),
	AB8500_PIN_HOG("GPIO17_P5", in_pd),
	AB8500_PIN_HOG("GPIO18_R5", in_pd),
	AB8500_PIN_HOG("GPIO19_U5", in_pd),
	AB8500_PIN_HOG("GPIO20_T5", in_pd),

	/*
	 * pins 21,22 and 23 are muxed in USB UICC
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("usbuicc_d_1", "usbuicc"),
	AB8500_PIN_HOG("GPIO21_H19", in_pd),
	AB8500_PIN_HOG("GPIO22_G20", in_pd),
	AB8500_PIN_HOG("GPIO23_G19", in_pd),

	/*
	 * pins 24,25 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio24_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO24_T14", in_pd),

	AB8500_MUX_HOG("gpio25_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO25_R16", in_pd),

	/*
	 * pins 26 is muxed in GPIO
	 * configured in OUTPUT LOW
	 */
	AB8500_MUX_HOG("gpio26_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO26_M16", out_lo),

	/*
	 * pins 27,28 are muxed in DMIC12
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic12_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO27_J6", in_pd),
	AB8500_PIN_HOG("GPIO28_K6", in_pd),

	/*
	 * pins 29,30 are muxed in DMIC34
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic34_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO29_G6", in_pd),
	AB8500_PIN_HOG("GPIO30_H6", in_pd),

	/*
	 * pins 31,32 are muxed in DMIC56
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("dmic56_d_1", "dmic"),
	AB8500_PIN_HOG("GPIO31_F5", in_pd),
	AB8500_PIN_HOG("GPIO32_G5", in_pd),

	/*
	 * pins 34 is muxed in EXTCPENA
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("extcpena_d_1", "extcpena"),
	AB8500_PIN_HOG("GPIO34_R17", in_pd),

	/*
	 * pins 35 is muxed in GPIO
	 * configured in OUTPUT LOW
	 */
	AB8500_MUX_HOG("gpio35_d_1", "gpio"),
	AB8500_PIN_HOG("GPIO35_W15", in_pd),

	/*
	 * pins 36,37,38 and 39 are muxed in GPIO
	 * configured in INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio36_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO36_A17", in_pd),

	AB8500_MUX_HOG("gpio37_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO37_E15", in_pd),

	AB8500_MUX_HOG("gpio38_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO38_C17", in_pd),

	AB8500_MUX_HOG("gpio39_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO39_E16", in_pd),

	/*
	 * pins 40 and 41 are muxed in MODCSLSDA
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("modsclsda_d_1", "modsclsda"),
	AB8500_PIN_HOG("GPIO40_T19", in_pd),
	AB8500_PIN_HOG("GPIO41_U19", in_pd),

	/*
	 * pins 42 is muxed in GPIO
	 * configured INPUT PULL DOWN
	 */
	AB8500_MUX_HOG("gpio42_a_1", "gpio"),
	AB8500_PIN_HOG("GPIO42_U2", in_pd),
};

static struct pinctrl_map __initdata ab8505_pinmap[] = {
	/* Sysclkreq2 */
	AB8505_MUX_STATE("sysclkreq2_d_1", "sysclkreq", "regulator.36", PINCTRL_STATE_DEFAULT),
	AB8505_PIN_STATE("GPIO1_N4", in_nopull, "regulator.36", PINCTRL_STATE_DEFAULT),
	/* sysclkreq2 disable, mux in gpio configured in input pulldown */
	AB8505_MUX_STATE("gpio1_a_1", "gpio", "regulator.36", PINCTRL_STATE_SLEEP),
	AB8505_PIN_STATE("GPIO1_N4", in_pd, "regulator.36", PINCTRL_STATE_SLEEP),

	/* pins 2 is muxed in GPIO, configured in INPUT PULL DOWN */
	AB8505_MUX_HOG("gpio2_a_1", "gpio"),
	AB8505_PIN_HOG("GPIO2_R5", in_pd),

	/* Sysclkreq4 */
	AB8505_MUX_STATE("sysclkreq4_d_1", "sysclkreq", "regulator.37", PINCTRL_STATE_DEFAULT),
	AB8505_PIN_STATE("GPIO3_P5", in_nopull, "regulator.37", PINCTRL_STATE_DEFAULT),
	/* sysclkreq4 disable, mux in gpio configured in input pulldown */
	AB8505_MUX_STATE("gpio3_a_1", "gpio", "regulator.37", PINCTRL_STATE_SLEEP),
	AB8505_PIN_STATE("GPIO3_P5", in_pd, "regulator.37", PINCTRL_STATE_SLEEP),

	AB8505_MUX_HOG("gpio10_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO10_B16", in_pd),

	AB8505_MUX_HOG("gpio11_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO11_B17", in_pd),

	AB8505_MUX_HOG("gpio13_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO13_D17", in_nopull),

	AB8505_MUX_HOG("pwmout1_d_1", "pwmout"),
	AB8505_PIN_HOG("GPIO14_C16", in_pd),

	AB8505_MUX_HOG("adi2_d_1", "adi2"),
	AB8505_PIN_HOG("GPIO17_P2", in_pd),
	AB8505_PIN_HOG("GPIO18_N3", in_pd),
	AB8505_PIN_HOG("GPIO19_T1", in_pd),
	AB8505_PIN_HOG("GPIO20_P3", in_pd),

	AB8505_MUX_HOG("gpio34_a_1", "gpio"),
	AB8505_PIN_HOG("GPIO34_H14", in_pd),

	AB8505_MUX_HOG("modsclsda_d_1", "modsclsda"),
	AB8505_PIN_HOG("GPIO40_J15", in_pd),
	AB8505_PIN_HOG("GPIO41_J14", in_pd),

	AB8505_MUX_HOG("gpio50_d_1", "gpio"),
	AB8505_PIN_HOG("GPIO50_L4", in_nopull),

	AB8505_MUX_HOG("resethw_d_1", "resethw"),
	AB8505_PIN_HOG("GPIO52_D16", in_pd),

	AB8505_MUX_HOG("service_d_1", "service"),
	AB8505_PIN_HOG("GPIO53_D15", in_pd),
};

/* Pin control settings */
static struct pinctrl_map __initdata mop500_family_pinmap[] = {
	/*
	 * uMSP0, mux in 4 pins, regular placement of RX/TX
	 * explicitly set the pins to no pull
	 */
	DB8500_MUX_HOG("msp0txrx_a_1", "msp0"),
	DB8500_MUX_HOG("msp0tfstck_a_1", "msp0"),
	DB8500_PIN_HOG("GPIO12_AC4", in_nopull), /* TXD */
	DB8500_PIN_HOG("GPIO15_AC3", in_nopull), /* RXD */
	DB8500_PIN_HOG("GPIO13_AF3", in_nopull), /* TFS */
	DB8500_PIN_HOG("GPIO14_AE3", in_nopull), /* TCK */
	/* MSP2 for HDMI, pull down TXD, TCK, TFS  */
	DB8500_MUX_HOG("msp2_a_1", "msp2"),
	DB8500_PIN_HOG("GPIO193_AH27", in_pd), /* TXD */
	DB8500_PIN_HOG("GPIO194_AF27", in_pd), /* TCK */
	DB8500_PIN_HOG("GPIO195_AG28", in_pd), /* TFS */
	DB8500_PIN_HOG("GPIO196_AG26", out_lo), /* RXD */
	/*
	 * LCD, set TE0 (using LCD VSI0) and D14 (touch screen interrupt) to
	 * pull-up
	 * TODO: is this really correct? Snowball doesn't have a LCD.
	 */
	DB8500_MUX_HOG("lcdvsi0_a_1", "lcd"),
	DB8500_PIN_HOG("GPIO68_E1", in_pu),
	DB8500_PIN_HOG("GPIO84_C2", gpio_in_pu),
	/*
	 * STMPE1601/tc35893 keypad IRQ GPIO 218
	 * TODO: set for snowball and HREF really??
	 */
	DB8500_PIN_HOG("GPIO218_AH11", gpio_in_pu),
	/*
	 * The following pin sets were known as "runtime pins" before being
	 * converted to the pinctrl model. Here we model them as "default"
	 * states.
	 */
	/* MSP1 for ALSA codec */
	DB8500_MUX_HOG("msp1txrx_a_1", "msp1"),
	DB8500_MUX_HOG("msp1_a_1", "msp1"),
	DB8500_PIN_HOG("GPIO33_AF2", out_lo_slpm_nowkup),
	DB8500_PIN_HOG("GPIO34_AE1", in_nopull_slpm_nowkup),
	DB8500_PIN_HOG("GPIO35_AE2", in_nopull_slpm_nowkup),
	DB8500_PIN_HOG("GPIO36_AG2", in_nopull_slpm_nowkup),
	/* Mux in LCD data lines 8 thru 11 and LCDA CLK for MCDE TVOUT */
	DB8500_MUX("lcd_d8_d11_a_1", "lcd", "mcde-tvout"),
	DB8500_MUX("lcdaclk_b_1", "lcda", "mcde-tvout"),
	/* Mux in LCD VSI1 and pull it up for MCDE HDMI output */
	DB8500_MUX("lcdvsi1_a_1", "lcd", "0-0070"),
	DB8500_PIN("GPIO69_E2", in_pu, "0-0070"),
	/* LCD VSI1 sleep state */
	DB8500_PIN_SLEEP("GPIO69_E2", slpm_in_wkup_pdis, "0-0070"),

	/* Mux in USB pins, drive STP high */
	/* USB default state */
	DB8500_MUX("usb_a_1", "usb", "ab8500-usb.0"),
	DB8500_PIN("GPIO257_AE29", out_hi, "ab8500-usb.0"), /* STP */
	/* USB sleep state */
	DB8500_PIN_SLEEP("GPIO256_AF28", slpm_wkup_pdis_en, "ab8500-usb.0"), /* NXT */
	DB8500_PIN_SLEEP("GPIO257_AE29", slpm_out_hi_wkup_pdis, "ab8500-usb.0"), /* STP */
	DB8500_PIN_SLEEP("GPIO258_AD29", slpm_wkup_pdis_en, "ab8500-usb.0"), /* XCLK */
	DB8500_PIN_SLEEP("GPIO259_AC29", slpm_wkup_pdis_en, "ab8500-usb.0"), /* DIR */
	DB8500_PIN_SLEEP("GPIO260_AD28", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT7 */
	DB8500_PIN_SLEEP("GPIO261_AD26", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT6 */
	DB8500_PIN_SLEEP("GPIO262_AE26", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT5 */
	DB8500_PIN_SLEEP("GPIO263_AG29", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT4 */
	DB8500_PIN_SLEEP("GPIO264_AE27", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT3 */
	DB8500_PIN_SLEEP("GPIO265_AD27", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT2 */
	DB8500_PIN_SLEEP("GPIO266_AC28", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT1 */
	DB8500_PIN_SLEEP("GPIO267_AC27", slpm_in_wkup_pdis_en, "ab8500-usb.0"), /* DAT0 */

	/* Mux in SPI2 pins on the "other C1" altfunction */
	DB8500_MUX("spi2_oc1_2", "spi2", "spi2"),
	DB8500_PIN("GPIO216_AG12", gpio_out_hi, "spi2"), /* FRM */
	DB8500_PIN("GPIO218_AH11", in_pd, "spi2"), /* RXD */
	DB8500_PIN("GPIO215_AH13", out_lo, "spi2"), /* TXD */
	DB8500_PIN("GPIO217_AH12", out_lo, "spi2"), /* CLK */
	/* SPI2 idle state */
	DB8500_PIN_IDLE("GPIO218_AH11", slpm_in_wkup_pdis, "spi2"), /* RXD */
	DB8500_PIN_IDLE("GPIO215_AH13", slpm_out_lo_wkup_pdis, "spi2"), /* TXD */
	DB8500_PIN_IDLE("GPIO217_AH12", slpm_wkup_pdis, "spi2"), /* CLK */
	/* SPI2 sleep state */
	DB8500_PIN_SLEEP("GPIO216_AG12", slpm_in_wkup_pdis, "spi2"), /* FRM */
	DB8500_PIN_SLEEP("GPIO218_AH11", slpm_in_wkup_pdis, "spi2"), /* RXD */
	DB8500_PIN_SLEEP("GPIO215_AH13", slpm_out_lo_wkup_pdis, "spi2"), /* TXD */
	DB8500_PIN_SLEEP("GPIO217_AH12", slpm_wkup_pdis, "spi2"), /* CLK */

	/* ske default state */
	DB8500_MUX("kp_a_2", "kp", "nmk-ske-keypad"),
	DB8500_PIN("GPIO153_B17", in_pd, "nmk-ske-keypad"), /* I7 */
	DB8500_PIN("GPIO154_C16", in_pd, "nmk-ske-keypad"), /* I6 */
	DB8500_PIN("GPIO155_C19", in_pd, "nmk-ske-keypad"), /* I5 */
	DB8500_PIN("GPIO156_C17", in_pd, "nmk-ske-keypad"), /* I4 */
	DB8500_PIN("GPIO161_D21", in_pd, "nmk-ske-keypad"), /* I3 */
	DB8500_PIN("GPIO162_D20", in_pd, "nmk-ske-keypad"), /* I2 */
	DB8500_PIN("GPIO163_C20", in_pd, "nmk-ske-keypad"), /* I1 */
	DB8500_PIN("GPIO164_B21", in_pd, "nmk-ske-keypad"), /* I0 */
	DB8500_PIN("GPIO157_A18", out_lo, "nmk-ske-keypad"), /* O7 */
	DB8500_PIN("GPIO158_C18", out_lo, "nmk-ske-keypad"), /* O6 */
	DB8500_PIN("GPIO159_B19", out_lo, "nmk-ske-keypad"), /* O5 */
	DB8500_PIN("GPIO160_B20", out_lo, "nmk-ske-keypad"), /* O4 */
	DB8500_PIN("GPIO165_C21", out_lo, "nmk-ske-keypad"), /* O3 */
	DB8500_PIN("GPIO166_A22", out_lo, "nmk-ske-keypad"), /* O2 */
	DB8500_PIN("GPIO167_B24", out_lo, "nmk-ske-keypad"), /* O1 */
	DB8500_PIN("GPIO168_C22", out_lo, "nmk-ske-keypad"), /* O0 */
	/* ske sleep state */
	DB8500_PIN_SLEEP("GPIO153_B17", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I7 */
	DB8500_PIN_SLEEP("GPIO154_C16", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I6 */
	DB8500_PIN_SLEEP("GPIO155_C19", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I5 */
	DB8500_PIN_SLEEP("GPIO156_C17", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I4 */
	DB8500_PIN_SLEEP("GPIO161_D21", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I3 */
	DB8500_PIN_SLEEP("GPIO162_D20", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I2 */
	DB8500_PIN_SLEEP("GPIO163_C20", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I1 */
	DB8500_PIN_SLEEP("GPIO164_B21", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I0 */
	DB8500_PIN_SLEEP("GPIO157_A18", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O7 */
	DB8500_PIN_SLEEP("GPIO158_C18", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O6 */
	DB8500_PIN_SLEEP("GPIO159_B19", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O5 */
	DB8500_PIN_SLEEP("GPIO160_B20", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O4 */
	DB8500_PIN_SLEEP("GPIO165_C21", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O3 */
	DB8500_PIN_SLEEP("GPIO166_A22", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O2 */
	DB8500_PIN_SLEEP("GPIO167_B24", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O1 */
	DB8500_PIN_SLEEP("GPIO168_C22", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O0 */

	/* STM APE pins states */
	DB8500_MUX_STATE("stmape_c_1", "stmape",
		"stm", "ape_mipi34"),
	DB8500_PIN_STATE("GPIO70_G5", in_nopull,
		"stm", "ape_mipi34"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", in_nopull,
		"stm", "ape_mipi34"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", in_nopull,
		"stm", "ape_mipi34"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", in_nopull,
		"stm", "ape_mipi34"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", in_nopull,
		"stm", "ape_mipi34"), /* dat0 */

	DB8500_PIN_STATE("GPIO70_G5", slpm_out_lo_pdis,
		"stm", "ape_mipi34_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", slpm_out_lo_pdis,
		"stm", "ape_mipi34_sleep"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", slpm_out_lo_pdis,
		"stm", "ape_mipi34_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", slpm_out_lo_pdis,
		"stm", "ape_mipi34_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", slpm_out_lo_pdis,
		"stm", "ape_mipi34_sleep"), /* dat0 */

	DB8500_MUX_STATE("stmape_oc1_1", "stmape",
		"stm", "ape_microsd"),
	DB8500_PIN_STATE("GPIO23_AA4", in_nopull,
		"stm", "ape_microsd"), /* clk */
	DB8500_PIN_STATE("GPIO25_Y4", in_nopull,
		"stm", "ape_microsd"), /* dat0 */
	DB8500_PIN_STATE("GPIO26_Y2", in_nopull,
		"stm", "ape_microsd"), /* dat1 */
	DB8500_PIN_STATE("GPIO27_AA2", in_nopull,
		"stm", "ape_microsd"), /* dat2 */
	DB8500_PIN_STATE("GPIO28_AA1", in_nopull,
		"stm", "ape_microsd"), /* dat3 */

	DB8500_PIN_STATE("GPIO23_AA4", slpm_out_lo_wkup_pdis,
		"stm", "ape_microsd_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO25_Y4", slpm_in_wkup_pdis,
		"stm", "ape_microsd_sleep"), /* dat0 */
	DB8500_PIN_STATE("GPIO26_Y2", slpm_in_wkup_pdis,
		"stm", "ape_microsd_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO27_AA2", slpm_in_wkup_pdis,
		"stm", "ape_microsd_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO28_AA1", slpm_in_wkup_pdis,
		"stm", "ape_microsd_sleep"), /* dat3 */

	/*  STM Modem pins states */
	DB8500_MUX_STATE("stmmod_oc3_2", "stmmod",
		"stm", "mod_mipi34"),
	DB8500_MUX_STATE("uartmodrx_oc3_1", "uartmod",
		"stm", "mod_mipi34"),
	DB8500_MUX_STATE("uartmodtx_oc3_1", "uartmod",
		"stm", "mod_mipi34"),
	DB8500_PIN_STATE("GPIO70_G5", in_nopull,
		"stm", "mod_mipi34"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", in_nopull,
		"stm", "mod_mipi34"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", in_nopull,
		"stm", "mod_mipi34"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", in_nopull,
		"stm", "mod_mipi34"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", in_nopull,
		"stm", "mod_mipi34"), /* dat0 */
	DB8500_PIN_STATE("GPIO75_H2", in_pu,
		"stm", "mod_mipi34"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", out_lo,
		"stm", "mod_mipi34"), /* uartmod tx */

	DB8500_PIN_STATE("GPIO70_G5", slpm_out_lo_pdis,
		"stm", "mod_mipi34_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", slpm_out_lo_pdis,
		"stm", "mod_mipi34_sleep"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", slpm_out_lo_pdis,
		"stm", "mod_mipi34_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", slpm_out_lo_pdis,
		"stm", "mod_mipi34_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", slpm_out_lo_pdis,
		"stm", "mod_mipi34_sleep"), /* dat0 */
	DB8500_PIN_STATE("GPIO75_H2", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_sleep"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", slpm_out_lo_wkup_pdis,
		"stm", "mod_mipi34_sleep"), /* uartmod tx */

	DB8500_MUX_STATE("stmmod_b_1", "stmmod",
		"stm", "mod_microsd"),
	DB8500_MUX_STATE("uartmodrx_oc3_1", "uartmod",
		"stm", "mod_microsd"),
	DB8500_MUX_STATE("uartmodtx_oc3_1", "uartmod",
		"stm", "mod_microsd"),
	DB8500_PIN_STATE("GPIO23_AA4", in_nopull,
		"stm", "mod_microsd"), /* clk */
	DB8500_PIN_STATE("GPIO25_Y4", in_nopull,
		"stm", "mod_microsd"), /* dat0 */
	DB8500_PIN_STATE("GPIO26_Y2", in_nopull,
		"stm", "mod_microsd"), /* dat1 */
	DB8500_PIN_STATE("GPIO27_AA2", in_nopull,
		"stm", "mod_microsd"), /* dat2 */
	DB8500_PIN_STATE("GPIO28_AA1", in_nopull,
		"stm", "mod_microsd"), /* dat3 */
	DB8500_PIN_STATE("GPIO75_H2", in_pu,
		"stm", "mod_microsd"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", out_lo,
		"stm", "mod_microsd"), /* uartmod tx */

	DB8500_PIN_STATE("GPIO23_AA4", slpm_out_lo_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO25_Y4", slpm_in_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* dat0 */
	DB8500_PIN_STATE("GPIO26_Y2", slpm_in_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO27_AA2", slpm_in_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO28_AA1", slpm_in_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* dat3 */
	DB8500_PIN_STATE("GPIO75_H2", slpm_in_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", slpm_out_lo_wkup_pdis,
		"stm", "mod_microsd_sleep"), /* uartmod tx */

	/*  STM dual Modem/APE pins state */
	DB8500_MUX_STATE("stmmod_oc3_2", "stmmod",
		"stm", "mod_mipi34_ape_mipi60"),
	DB8500_MUX_STATE("stmape_c_2", "stmape",
		"stm", "mod_mipi34_ape_mipi60"),
	DB8500_MUX_STATE("uartmodrx_oc3_1", "uartmod",
		"stm", "mod_mipi34_ape_mipi60"),
	DB8500_MUX_STATE("uartmodtx_oc3_1", "uartmod",
		"stm", "mod_mipi34_ape_mipi60"),
	DB8500_PIN_STATE("GPIO70_G5", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat0 */
	DB8500_PIN_STATE("GPIO75_H2", in_pu,
		"stm", "mod_mipi34_ape_mipi60"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", out_lo,
		"stm", "mod_mipi34_ape_mipi60"), /* uartmod tx */
	DB8500_PIN_STATE("GPIO155_C19", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* clk */
	DB8500_PIN_STATE("GPIO156_C17", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat3 */
	DB8500_PIN_STATE("GPIO157_A18", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat2 */
	DB8500_PIN_STATE("GPIO158_C18", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat1 */
	DB8500_PIN_STATE("GPIO159_B19", in_nopull,
		"stm", "mod_mipi34_ape_mipi60"), /* dat0 */

	DB8500_PIN_STATE("GPIO70_G5", slpm_out_lo_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO71_G4", slpm_out_lo_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat3 */
	DB8500_PIN_STATE("GPIO72_H4", slpm_out_lo_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO73_H3", slpm_out_lo_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO74_J3", slpm_out_lo_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat0 */
	DB8500_PIN_STATE("GPIO75_H2", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* uartmod rx */
	DB8500_PIN_STATE("GPIO76_J2", slpm_out_lo_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* uartmod tx */
	DB8500_PIN_STATE("GPIO155_C19", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* clk */
	DB8500_PIN_STATE("GPIO156_C17", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat3 */
	DB8500_PIN_STATE("GPIO157_A18", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat2 */
	DB8500_PIN_STATE("GPIO158_C18", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat1 */
	DB8500_PIN_STATE("GPIO159_B19", slpm_in_wkup_pdis,
		"stm", "mod_mipi34_ape_mipi60_sleep"), /* dat0 */
};

/*
 * These are specifically for the MOP500 and HREFP (pre-v60) version of the
 * board, which utilized a TC35892 GPIO expander instead of using a lot of
 * on-chip pins as the HREFv60 and later does.
 */
static struct pinctrl_map __initdata mop500_pinmap[] = {
	/* Mux in SSP0, pull down RXD pin */
	DB8500_MUX_HOG("ssp0_a_1", "ssp0"),
	DB8500_PIN_HOG("GPIO145_C13", pd),
	/*
	 * XENON Flashgun on image processor GPIO (controlled from image
	 * processor firmware), mux in these image processor GPIO lines 0
	 * (XENON_FLASH_ID) and 1 (XENON_READY) on altfunction C and pull up
	 * the pins.
	 */
	DB8500_MUX_HOG("ipgpio0_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio1_c_1", "ipgpio"),
	DB8500_PIN_HOG("GPIO6_AF6", in_pu),
	DB8500_PIN_HOG("GPIO7_AG5", in_pu),
	/* TC35892 IRQ, pull up the line, let the driver mux in the pin */
	DB8500_PIN_HOG("GPIO217_AH12", gpio_in_pu),
	/*
	 * Runtime stuff: make it possible to mux in the SKE keypad
	 * and bias the pins
	 */
	/* ske default state */
	DB8500_MUX("kp_a_2", "kp", "nmk-ske-keypad"),
	DB8500_PIN("GPIO153_B17", in_pu, "nmk-ske-keypad"), /* I7 */
	DB8500_PIN("GPIO154_C16", in_pu, "nmk-ske-keypad"), /* I6 */
	DB8500_PIN("GPIO155_C19", in_pu, "nmk-ske-keypad"), /* I5 */
	DB8500_PIN("GPIO156_C17", in_pu, "nmk-ske-keypad"), /* I4 */
	DB8500_PIN("GPIO161_D21", in_pu, "nmk-ske-keypad"), /* I3 */
	DB8500_PIN("GPIO162_D20", in_pu, "nmk-ske-keypad"), /* I2 */
	DB8500_PIN("GPIO163_C20", in_pu, "nmk-ske-keypad"), /* I1 */
	DB8500_PIN("GPIO164_B21", in_pu, "nmk-ske-keypad"), /* I0 */
	DB8500_PIN("GPIO157_A18", out_lo, "nmk-ske-keypad"), /* O7 */
	DB8500_PIN("GPIO158_C18", out_lo, "nmk-ske-keypad"), /* O6 */
	DB8500_PIN("GPIO159_B19", out_lo, "nmk-ske-keypad"), /* O5 */
	DB8500_PIN("GPIO160_B20", out_lo, "nmk-ske-keypad"), /* O4 */
	DB8500_PIN("GPIO165_C21", out_lo, "nmk-ske-keypad"), /* O3 */
	DB8500_PIN("GPIO166_A22", out_lo, "nmk-ske-keypad"), /* O2 */
	DB8500_PIN("GPIO167_B24", out_lo, "nmk-ske-keypad"), /* O1 */
	DB8500_PIN("GPIO168_C22", out_lo, "nmk-ske-keypad"), /* O0 */
	/* ske sleep state */
	DB8500_PIN_SLEEP("GPIO153_B17", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I7 */
	DB8500_PIN_SLEEP("GPIO154_C16", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I6 */
	DB8500_PIN_SLEEP("GPIO155_C19", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I5 */
	DB8500_PIN_SLEEP("GPIO156_C17", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I4 */
	DB8500_PIN_SLEEP("GPIO161_D21", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I3 */
	DB8500_PIN_SLEEP("GPIO162_D20", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I2 */
	DB8500_PIN_SLEEP("GPIO163_C20", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I1 */
	DB8500_PIN_SLEEP("GPIO164_B21", slpm_in_pu_wkup_pdis_en, "nmk-ske-keypad"), /* I0 */
	DB8500_PIN_SLEEP("GPIO157_A18", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O7 */
	DB8500_PIN_SLEEP("GPIO158_C18", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O6 */
	DB8500_PIN_SLEEP("GPIO159_B19", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O5 */
	DB8500_PIN_SLEEP("GPIO160_B20", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O4 */
	DB8500_PIN_SLEEP("GPIO165_C21", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O3 */
	DB8500_PIN_SLEEP("GPIO166_A22", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O2 */
	DB8500_PIN_SLEEP("GPIO167_B24", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O1 */
	DB8500_PIN_SLEEP("GPIO168_C22", slpm_out_lo_pdis, "nmk-ske-keypad"), /* O0 */
};

/*
 * The HREFv60 series of platforms is using available pins on the DB8500
 * insteaf of the Toshiba I2C GPIO expander, reusing some pins like the SSP0
 * and SSP1 ports (previously connected to the AB8500) as generic GPIO lines.
 */
static struct pinctrl_map __initdata hrefv60_pinmap[] = {
	/* Drive WLAN_ENA low */
	DB8500_PIN_HOG("GPIO85_D5", gpio_out_lo), /* WLAN_ENA */
	/*
	 * XENON Flashgun on image processor GPIO (controlled from image
	 * processor firmware), mux in these image processor GPIO lines 0
	 * (XENON_FLASH_ID), 1 (XENON_READY) and there is an assistant
	 * LED on IP GPIO 4 (XENON_EN2) on altfunction C, that need bias
	 * from GPIO21 so pull up 0, 1 and drive 4 and GPIO21 low as output.
	 */
	DB8500_MUX_HOG("ipgpio0_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio1_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio4_c_1", "ipgpio"),
	DB8500_PIN_HOG("GPIO6_AF6", in_pu), /* XENON_FLASH_ID */
	DB8500_PIN_HOG("GPIO7_AG5", in_pu), /* XENON_READY */
	DB8500_PIN_HOG("GPIO21_AB3", gpio_out_lo), /* XENON_EN1 */
	DB8500_PIN_HOG("GPIO64_F3", out_lo), /* XENON_EN2 */
	/* Magnetometer uses GPIO 31 and 32, pull these up/down respectively */
	DB8500_PIN_HOG("GPIO31_V3", gpio_in_pu), /* EN1 */
	DB8500_PIN_HOG("GPIO32_V2", gpio_in_pd), /* DRDY */
	/*
	 * Display Interface 1 uses GPIO 65 for RST (reset).
	 * Display Interface 2 uses GPIO 66 for RST (reset).
	 * Drive DISP1 reset high (not reset), driver DISP2 reset low (reset)
	 */
	DB8500_PIN_HOG("GPIO65_F1", gpio_out_hi), /* DISP1 NO RST */
	DB8500_PIN_HOG("GPIO66_G3", gpio_out_lo), /* DISP2 RST */
	/*
	 * Touch screen uses GPIO 143 for RST1, GPIO 146 for RST2 and
	 * GPIO 67 for interrupts. Pull-up the IRQ line and drive both
	 * reset signals low.
	 */
	DB8500_PIN_HOG("GPIO143_D12", gpio_out_lo), /* TOUCH_RST1 */
	DB8500_PIN_HOG("GPIO67_G2", gpio_in_pu), /* TOUCH_INT2 */
	DB8500_PIN_HOG("GPIO146_D13", gpio_out_lo), /* TOUCH_RST2 */
	/*
	 * Drive D19-D23 for the ETM PTM trace interface low,
	 * (presumably pins are unconnected therefore grounded here,
	 * the "other alt C1" setting enables these pins)
	 */
	DB8500_PIN_HOG("GPIO70_G5", gpio_out_lo),
	DB8500_PIN_HOG("GPIO71_G4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO72_H4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO73_H3", gpio_out_lo),
	DB8500_PIN_HOG("GPIO74_J3", gpio_out_lo),
	/* NAHJ CTRL on GPIO 76 to low, CTRL_INV on GPIO216 to high */
	DB8500_PIN_HOG("GPIO76_J2", gpio_out_lo), /* CTRL */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_out_hi), /* CTRL_INV */
	/* NFC ENA and RESET to low, pulldown IRQ line */
	DB8500_PIN_HOG("GPIO77_H1", gpio_out_lo), /* NFC_ENA */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pd), /* NFC_IRQ */
	DB8500_PIN_HOG("GPIO142_C11", gpio_out_lo), /* NFC_RESET */
	/*
	 * SKE keyboard partly on alt A and partly on "Other alt C1"
	 * Driver KP_O1,2,3,6,7 low and pull up KP_I 0,2,3 for three
	 * rows of 6 keys, then pull up force sensing interrup and
	 * drive reset and force sensing WU low.
	 */
	DB8500_MUX_HOG("kp_a_1", "kp"),
	DB8500_MUX_HOG("kp_oc1_1", "kp"),
	DB8500_PIN_HOG("GPIO90_A3", out_lo), /* KP_O1 */
	DB8500_PIN_HOG("GPIO87_B3", out_lo), /* KP_O2 */
	DB8500_PIN_HOG("GPIO86_C6", out_lo), /* KP_O3 */
	DB8500_PIN_HOG("GPIO96_D8", out_lo), /* KP_O6 */
	DB8500_PIN_HOG("GPIO94_D7", out_lo), /* KP_O7 */
	DB8500_PIN_HOG("GPIO93_B7", in_pu), /* KP_I0 */
	DB8500_PIN_HOG("GPIO89_E6", in_pu), /* KP_I2 */
	DB8500_PIN_HOG("GPIO88_C4", in_pu), /* KP_I3 */
	DB8500_PIN_HOG("GPIO91_B6", gpio_in_pu), /* FORCE_SENSING_INT */
	DB8500_PIN_HOG("GPIO92_D6", gpio_out_lo), /* FORCE_SENSING_RST */
	DB8500_PIN_HOG("GPIO97_D9", gpio_out_lo), /* FORCE_SENSING_WU */
	/* DiPro Sensor interrupt */
	DB8500_PIN_HOG("GPIO139_C9", gpio_in_pu), /* DIPRO_INT */
	/* Audio Amplifier HF enable */
	DB8500_PIN_HOG("GPIO149_B14", gpio_out_hi), /* VAUDIO_HF_EN, enable MAX8968 */
	/* GBF interface, pull low to reset state */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_lo), /* GBF_ENA_RESET */
	/* MSP : HDTV INTERFACE GPIO line */
	DB8500_PIN_HOG("GPIO192_AJ27", gpio_in_pd),
	/* Accelerometer interrupt lines */
	DB8500_PIN_HOG("GPIO82_C1", gpio_in_pu), /* ACC_INT1 */
	DB8500_PIN_HOG("GPIO83_D3", gpio_in_pu), /* ACC_INT2 */
	/*
	 * Runtime stuff
	 * Pull up/down of some sensor GPIO pins, for proximity, HAL sensor
	 * etc.
	 */
	DB8500_PIN("GPIO217_AH12", gpio_in_pu_slpm_gpio_nopull, "gpio-keys.0"),
	DB8500_PIN("GPIO145_C13", gpio_in_pd_slpm_gpio_nopull, "gpio-keys.0"),
	DB8500_PIN("GPIO139_C9", gpio_in_pu_slpm_gpio_nopull, "gpio-keys.0"),
};

static struct pinctrl_map __initdata u9500_pinmap[] = {
	/* WLAN_IRQ line */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pu),
	/* HSI */
	DB8500_MUX_HOG("hsir_a_1", "hsi"),
	DB8500_MUX_HOG("hsit_a_2", "hsi"),
	DB8500_PIN_HOG("GPIO219_AG10", in_pd), /* RX FLA0 */
	DB8500_PIN_HOG("GPIO220_AH10", in_pd), /* RX DAT0 */
	DB8500_PIN_HOG("GPIO221_AJ11", out_lo), /* RX RDY0 */
	DB8500_PIN_HOG("GPIO222_AJ9", out_lo), /* TX FLA0 */
	DB8500_PIN_HOG("GPIO223_AH9", out_lo), /* TX DAT0 */
	DB8500_PIN_HOG("GPIO224_AG9", in_pd), /* TX RDY0 */
	DB8500_PIN_HOG("GPIO225_AG8", in_pd), /* CAWAKE0 */
	DB8500_PIN_HOG("GPIO226_AF8", gpio_out_hi), /* ACWAKE0 */
};

static struct pinctrl_map __initdata u8500_pinmap[] = {
	DB8500_PIN_HOG("GPIO226_AF8", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO4_AH6", gpio_in_pu), /* WLAN_IRQ */
};

static struct pinctrl_map __initdata snowball_pinmap[] = {
	/* Mux in SSP0 connected to AB8500, pull down RXD pin */
	DB8500_MUX_HOG("ssp0_a_1", "ssp0"),
	DB8500_PIN_HOG("GPIO145_C13", pd),
	/* Mux in "SM" which is used for the SMSC911x Ethernet adapter */
	DB8500_MUX_HOG("sm_b_1", "sm"),
	/* User LED */
	DB8500_PIN_HOG("GPIO142_C11", gpio_out_hi),
	/* Drive RSTn_LAN high */
	DB8500_PIN_HOG("GPIO141_C12", gpio_out_hi),
	/*  Accelerometer/Magnetometer */
	DB8500_PIN_HOG("GPIO163_C20", gpio_in_pu), /* ACCEL_IRQ1 */
	DB8500_PIN_HOG("GPIO164_B21", gpio_in_pu), /* ACCEL_IRQ2 */
	DB8500_PIN_HOG("GPIO165_C21", gpio_in_pu), /* MAG_DRDY */
	/* WLAN/GBF */
	DB8500_PIN_HOG("GPIO161_D21", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_hi), /* GBF_ENA */
	DB8500_PIN_HOG("GPIO215_AH13", gpio_out_lo), /* WLAN_ENA */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_in_pu), /* WLAN_IRQ */
};

/*
 * passing "pinsfor=" in kernel cmdline allows for custom
 * configuration of GPIOs on u8500 derived boards.
 */
static int __init early_pinsfor(char *p)
{
	pinsfor = PINS_FOR_DEFAULT;

	if (strcmp(p, "u9500-21") == 0)
		pinsfor = PINS_FOR_U9500;

	return 0;
}
early_param("pinsfor", early_pinsfor);

int pins_for_u9500(void)
{
	if (pinsfor == PINS_FOR_U9500)
		return 1;

	return 0;
}

static void __init mop500_href_family_pinmaps_init(void)
{
	switch (pinsfor) {
	case PINS_FOR_U9500:
		pinctrl_register_mappings(u9500_pinmap,
					  ARRAY_SIZE(u9500_pinmap));
		break;
	case PINS_FOR_DEFAULT:
		pinctrl_register_mappings(u8500_pinmap,
					  ARRAY_SIZE(u8500_pinmap));
	default:
		break;
	}
}

void __init mop500_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(mop500_pinmap,
				  ARRAY_SIZE(mop500_pinmap));
	mop500_href_family_pinmaps_init();
	if (machine_is_u8520())
		pinctrl_register_mappings(ab8505_pinmap,
					  ARRAY_SIZE(ab8505_pinmap));
	else
		pinctrl_register_mappings(ab8500_pinmap,
					  ARRAY_SIZE(ab8500_pinmap));
}

void __init snowball_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(snowball_pinmap,
				  ARRAY_SIZE(snowball_pinmap));
	pinctrl_register_mappings(u8500_pinmap,
				  ARRAY_SIZE(u8500_pinmap));
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}

void __init hrefv60_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(hrefv60_pinmap,
				  ARRAY_SIZE(hrefv60_pinmap));
	mop500_href_family_pinmaps_init();
	pinctrl_register_mappings(ab8500_pinmap,
				  ARRAY_SIZE(ab8500_pinmap));
}
