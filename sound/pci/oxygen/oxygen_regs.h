#ifndef OXYGEN_REGS_H_INCLUDED
#define OXYGEN_REGS_H_INCLUDED

/* recording channel A */
#define OXYGEN_DMA_A_ADDRESS		0x00	/* 32-bit base address */
#define OXYGEN_DMA_A_COUNT		0x04	/* buffer counter (dwords) */
#define OXYGEN_DMA_A_TCOUNT		0x06	/* interrupt counter (dwords) */

/* recording channel B */
#define OXYGEN_DMA_B_ADDRESS		0x08
#define OXYGEN_DMA_B_COUNT		0x0c
#define OXYGEN_DMA_B_TCOUNT		0x0e

/* recording channel C */
#define OXYGEN_DMA_C_ADDRESS		0x10
#define OXYGEN_DMA_C_COUNT		0x14
#define OXYGEN_DMA_C_TCOUNT		0x16

/* SPDIF playback channel */
#define OXYGEN_DMA_SPDIF_ADDRESS	0x18
#define OXYGEN_DMA_SPDIF_COUNT		0x1c
#define OXYGEN_DMA_SPDIF_TCOUNT		0x1e

/* multichannel playback channel */
#define OXYGEN_DMA_MULTICH_ADDRESS	0x20
#define OXYGEN_DMA_MULTICH_COUNT	0x24	/* 32 bits */
#define OXYGEN_DMA_MULTICH_TCOUNT	0x28	/* 32 bits */

/* AC'97 (front panel) playback channel */
#define OXYGEN_DMA_AC97_ADDRESS		0x30
#define OXYGEN_DMA_AC97_COUNT		0x34
#define OXYGEN_DMA_AC97_TCOUNT		0x36

/* all registers 0x00..0x36 return current position on read */

#define OXYGEN_DMA_STATUS		0x40	/* 1 = running, 0 = stop */
#define  OXYGEN_CHANNEL_A		0x01
#define  OXYGEN_CHANNEL_B		0x02
#define  OXYGEN_CHANNEL_C		0x04
#define  OXYGEN_CHANNEL_SPDIF		0x08
#define  OXYGEN_CHANNEL_MULTICH		0x10
#define  OXYGEN_CHANNEL_AC97		0x20

#define OXYGEN_DMA_RESET		0x42
/* OXYGEN_CHANNEL_* */

#define OXYGEN_PLAY_CHANNELS		0x43
#define  OXYGEN_PLAY_CHANNELS_MASK	0x03
#define  OXYGEN_PLAY_CHANNELS_2		0x00
#define  OXYGEN_PLAY_CHANNELS_4		0x01
#define  OXYGEN_PLAY_CHANNELS_6		0x02
#define  OXYGEN_PLAY_CHANNELS_8		0x03

#define OXYGEN_INTERRUPT_MASK		0x44
/* OXYGEN_CHANNEL_* */
#define  OXYGEN_INT_SPDIF_IN_CHANGE	0x0100
#define  OXYGEN_INT_GPIO		0x0800

#define OXYGEN_INTERRUPT_STATUS		0x46
/* OXYGEN_CHANNEL_* amd OXYGEN_INT_* */
#define  OXYGEN_INT_MIDI		0x1000

#define OXYGEN_MISC			0x48
#define  OXYGEN_MISC_MAGIC		0x20
#define  OXYGEN_MISC_MIDI		0x40

#define OXYGEN_REC_FORMAT		0x4a
#define  OXYGEN_REC_FORMAT_A_MASK	0x03
#define  OXYGEN_REC_FORMAT_A_SHIFT	0
#define  OXYGEN_REC_FORMAT_B_MASK	0x0c
#define  OXYGEN_REC_FORMAT_B_SHIFT	2
#define  OXYGEN_REC_FORMAT_C_MASK	0x30
#define  OXYGEN_REC_FORMAT_C_SHIFT	4
#define  OXYGEN_FORMAT_16		0x00
#define  OXYGEN_FORMAT_24		0x01
#define  OXYGEN_FORMAT_32		0x02

#define OXYGEN_PLAY_FORMAT		0x4b
#define  OXYGEN_SPDIF_FORMAT_MASK	0x03
#define  OXYGEN_SPDIF_FORMAT_SHIFT	0
#define  OXYGEN_MULTICH_FORMAT_MASK	0x0c
#define  OXYGEN_MULTICH_FORMAT_SHIFT	2
#define  OXYGEN_AC97_FORMAT_MASK	0x30
#define  OXYGEN_AC97_FORMAT_SHIFT	4
/* OXYGEN_FORMAT_* */

#define OXYGEN_REC_CHANNELS		0x4c
#define  OXYGEN_REC_A_CHANNELS_MASK	0x07
#define  OXYGEN_REC_CHANNELS_2		0x00
#define  OXYGEN_REC_CHANNELS_4		0x01
#define  OXYGEN_REC_CHANNELS_6		0x03	/* or 0x02 */
#define  OXYGEN_REC_CHANNELS_8		0x04

#define OXYGEN_FUNCTION			0x50
#define  OXYGEN_FUNCTION_RESET_CODEC	0x02
#define  OXYGEN_FUNCTION_ENABLE_SPI_4_5	0x80

#define OXYGEN_I2S_MULTICH_FORMAT	0x60
#define  OXYGEN_I2S_RATE_MASK		0x0007
#define  OXYGEN_RATE_32000		0x0000
#define  OXYGEN_RATE_44100		0x0001
#define  OXYGEN_RATE_48000		0x0002
#define  OXYGEN_RATE_64000		0x0003
#define  OXYGEN_RATE_88200		0x0004
#define  OXYGEN_RATE_96000		0x0005
#define  OXYGEN_RATE_176400		0x0006
#define  OXYGEN_RATE_192000		0x0007
#define  OXYGEN_I2S_FORMAT_MASK		0x0008
#define  OXYGEN_I2S_FORMAT_I2S		0x0000
#define  OXYGEN_I2S_FORMAT_LJUST	0x0008
#define  OXYGEN_I2S_MAGIC2_MASK		0x0030
#define  OXYGEN_I2S_BITS_MASK		0x00c0
#define  OXYGEN_I2S_BITS_16		0x0000
#define  OXYGEN_I2S_BITS_20		0x0040
#define  OXYGEN_I2S_BITS_24		0x0080
#define  OXYGEN_I2S_BITS_32		0x00c0

#define OXYGEN_I2S_A_FORMAT		0x62
#define OXYGEN_I2S_B_FORMAT		0x64
#define OXYGEN_I2S_C_FORMAT		0x66
/* like OXYGEN_I2S_MULTICH_FORMAT */

#define OXYGEN_SPDIF_CONTROL		0x70
#define  OXYGEN_SPDIF_OUT_ENABLE	0x00000002
#define  OXYGEN_SPDIF_LOOPBACK		0x00000004
#define  OXYGEN_SPDIF_MAGIC2		0x00000020
#define  OXYGEN_SPDIF_MAGIC3		0x00000040
#define  OXYGEN_SPDIF_IN_VALID		0x00001000
#define  OXYGEN_SPDIF_IN_CHANGE		0x00008000	/* r/wc */
#define  OXYGEN_SPDIF_IN_INVERT		0x00010000	/* ? */
#define  OXYGEN_SPDIF_OUT_RATE_MASK	0x07000000
#define  OXYGEN_SPDIF_OUT_RATE_SHIFT	24
/* OXYGEN_RATE_* << OXYGEN_SPDIF_OUT_RATE_SHIFT */

#define OXYGEN_SPDIF_OUTPUT_BITS	0x74
#define  OXYGEN_SPDIF_NONAUDIO		0x00000002
#define  OXYGEN_SPDIF_C			0x00000004
#define  OXYGEN_SPDIF_PREEMPHASIS	0x00000008
#define  OXYGEN_SPDIF_CATEGORY_MASK	0x000007f0
#define  OXYGEN_SPDIF_CATEGORY_SHIFT	4
#define  OXYGEN_SPDIF_ORIGINAL		0x00000800
#define  OXYGEN_SPDIF_CS_RATE_MASK	0x0000f000
#define  OXYGEN_SPDIF_CS_RATE_SHIFT	12
#define  OXYGEN_SPDIF_V			0x00010000	/* 0 = valid */

#define OXYGEN_SPDIF_INPUT_BITS		0x78
/* 32 bits, IEC958_AES_* */

#define OXYGEN_2WIRE_CONTROL		0x90
#define  OXYGEN_2WIRE_DIR_MASK		0x01
#define  OXYGEN_2WIRE_DIR_WRITE		0x00	/* ? */
#define  OXYGEN_2WIRE_DIR_READ		0x01	/* ? */
#define  OXYGEN_2WIRE_ADDRESS_MASK	0xfe	/* slave device address */
#define  OXYGEN_2WIRE_ADDRESS_SHIFT	1

#define OXYGEN_2WIRE_MAP		0x91	/* address, 8 bits */
#define OXYGEN_2WIRE_DATA		0x92	/* data, 16 bits */

#define OXYGEN_2WIRE_BUS_STATUS		0x94
#define  OXYGEN_2WIRE_BUSY		0x01

#define OXYGEN_SPI_CONTROL		0x98
#define  OXYGEN_SPI_BUSY		0x01	/* read */
#define  OXYGEN_SPI_TRIGGER_WRITE	0x01	/* write */
#define  OXYGEN_SPI_DATA_LENGTH_MASK	0x02
#define  OXYGEN_SPI_DATA_LENGTH_2	0x00
#define  OXYGEN_SPI_DATA_LENGTH_3	0x02
#define  OXYGEN_SPI_CODEC_MASK		0x70	/* 0..5 */
#define  OXYGEN_SPI_CODEC_SHIFT		4
#define  OXYGEN_SPI_MAGIC		0x80

#define OXYGEN_SPI_DATA1		0x99
#define OXYGEN_SPI_DATA2		0x9a
#define OXYGEN_SPI_DATA3		0x9b

#define OXYGEN_MPU401			0xa0

#define OXYGEN_GPI_DATA			0xa4

#define OXYGEN_GPI_INTERRUPT_MASK	0xa5

#define OXYGEN_GPIO_DATA		0xa6

#define OXYGEN_GPIO_CONTROL		0xa8
/* 0: input, 1: output */

#define OXYGEN_GPIO_INTERRUPT_MASK	0xaa

#define OXYGEN_DEVICE_SENSE		0xac	/* ? */

#define OXYGEN_PLAY_ROUTING		0xc0
#define  OXYGEN_PLAY_DAC0_SOURCE_MASK	0x0300
#define  OXYGEN_PLAY_DAC1_SOURCE_MASK	0x0700
#define  OXYGEN_PLAY_DAC2_SOURCE_MASK	0x3000
#define  OXYGEN_PLAY_DAC3_SOURCE_MASK	0x7000

#define OXYGEN_REC_ROUTING		0xc2

#define OXYGEN_ADC_MONITOR		0xc3
#define  OXYGEN_ADC_MONITOR_MULTICH	0x01
#define  OXYGEN_ADC_MONITOR_AC97	0x04
#define  OXYGEN_ADC_MONITOR_SPDIF	0x10

#define OXYGEN_A_MONITOR_ROUTING	0xc4

#define OXYGEN_AC97_CONTROL		0xd0
#define  OXYGEN_AC97_RESET1		0x0001
#define  OXYGEN_AC97_RESET1_BUSY	0x0002
#define  OXYGEN_AC97_RESET2		0x0008
#define  OXYGEN_AC97_CODEC_0		0x0010
#define  OXYGEN_AC97_CODEC_1		0x0020

#define OXYGEN_AC97_INTERRUPT_MASK	0xd2

#define OXYGEN_AC97_INTERRUPT_STATUS	0xd3
#define  OXYGEN_AC97_READ_COMPLETE	0x01
#define  OXYGEN_AC97_WRITE_COMPLETE	0x02

#define OXYGEN_AC97_OUT_CONFIG		0xd4
#define  OXYGEN_AC97_OUT_MAGIC1		0x00000011
#define  OXYGEN_AC97_OUT_MAGIC2		0x00000033
#define  OXYGEN_AC97_OUT_MAGIC3		0x0000ff00

#define OXYGEN_AC97_IN_CONFIG		0xd8
#define  OXYGEN_AC97_IN_MAGIC1		0x00000011
#define  OXYGEN_AC97_IN_MAGIC2		0x00000033
#define  OXYGEN_AC97_IN_MAGIC3		0x00000300

#define OXYGEN_AC97_REGS		0xdc
#define  OXYGEN_AC97_REG_DATA_MASK	0x0000ffff
#define  OXYGEN_AC97_REG_ADDR_MASK	0x007f0000
#define  OXYGEN_AC97_REG_ADDR_SHIFT	16
#define  OXYGEN_AC97_REG_DIR_MASK	0x00800000
#define  OXYGEN_AC97_REG_DIR_WRITE	0x00000000
#define  OXYGEN_AC97_REG_DIR_READ	0x00800000
#define  OXYGEN_AC97_REG_CODEC_MASK	0x01000000
#define  OXYGEN_AC97_REG_CODEC_SHIFT	24

#define OXYGEN_DMA_FLUSH		0xe1
/* OXYGEN_CHANNEL_* */

#define OXYGEN_CODEC_VERSION		0xe4

#define OXYGEN_REVISION			0xe6
#define  OXYGEN_REVISION_2		0x08	/* bit flag */
#define  OXYGEN_REVISION_8787		0x14	/* all 8 bits */

#endif
