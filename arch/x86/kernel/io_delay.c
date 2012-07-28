/*
 * I/O delay strategies for inb_p/outb_p
 *
 * Allow for a DMI based override of port 0x80, needed for certain HP laptops
 * and possibly other systems. Also allow for the gradual elimination of
 * outb_p/inb_p API uses.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/io.h>

int io_delay_type __read_mostly = CONFIG_DEFAULT_IO_DELAY_TYPE;

static int __initdata io_delay_override;

/*
 * Paravirt wants native_io_delay to be a constant.
 */
void native_io_delay(void)
{
	switch (io_delay_type) {
	default:
	case CONFIG_IO_DELAY_TYPE_0X80:
		asm volatile ("outb %al, $0x80");
		break;
	case CONFIG_IO_DELAY_TYPE_0XED:
		asm volatile ("outb %al, $0xed");
		break;
	case CONFIG_IO_DELAY_TYPE_UDELAY:
		/*
		 * 2 usecs is an upper-bound for the outb delay but
		 * note that udelay doesn't have the bus-level
		 * side-effects that outb does, nor does udelay() have
		 * precise timings during very early bootup (the delays
		 * are shorter until calibrated):
		 */
		udelay(2);
	case CONFIG_IO_DELAY_TYPE_NONE:
		break;
	}
}
EXPORT_SYMBOL(native_io_delay);
/* 특정 머신에서는 0x80포트를 delay 포트로 쓰지 않고 0xed를 사용한다. */
static int __init dmi_io_delay_0xed_port(const struct dmi_system_id *id)
{
	/* 80과 타입이 같으면 기본 config에서는 ED 포트 사용  */
	if (io_delay_type == CONFIG_IO_DELAY_TYPE_0X80) {
		pr_notice("%s: using 0xed I/O delay port\n", id->ident);
		io_delay_type = CONFIG_IO_DELAY_TYPE_0XED;
	}

	return 0;
}

/*
 * Quirk table for systems that misbehave (lock up, etc.) if port
 * 0x80 is used:
 */
static struct dmi_system_id __initdata io_delay_0xed_port_dmi_table[] = {
	{
		.callback	= dmi_io_delay_0xed_port,
		.ident		= "Compaq Presario V6000",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR,	"Quanta"),
			DMI_MATCH(DMI_BOARD_NAME,	"30B7")
		}
	},
	{
		.callback	= dmi_io_delay_0xed_port,
		.ident		= "HP Pavilion dv9000z",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR,	"Quanta"),
			DMI_MATCH(DMI_BOARD_NAME,	"30B9")
		}
	},
	{
		.callback	= dmi_io_delay_0xed_port,
		.ident		= "HP Pavilion dv6000",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR,	"Quanta"),
			DMI_MATCH(DMI_BOARD_NAME,	"30B8")
		}
	},
	{
		.callback	= dmi_io_delay_0xed_port,
		.ident		= "HP Pavilion tx1000",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR,	"Quanta"),
			DMI_MATCH(DMI_BOARD_NAME,	"30BF")
		}
	},
	{
		.callback	= dmi_io_delay_0xed_port,
		.ident		= "Presario F700",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR,	"Quanta"),
			DMI_MATCH(DMI_BOARD_NAME,	"30D3")
		}
	},
	{ }
};
/* io_delay 타입을 결정한다. */
void __init io_delay_init(void)
{
	/* io_delay 파라미터가 세팅되지 않았다면 (기본으로) delay port로 ed를 등록 */
	if (!io_delay_override)
		/* 체크해서 특정 하드웨어에서는 0x80포트가 아닌 0xed 포트를 사용.  */
		dmi_check_system(io_delay_0xed_port_dmi_table);
}

static int __init io_delay_param(char *s)
{
	if (!s)
		return -EINVAL;

	if (!strcmp(s, "0x80"))
		io_delay_type = CONFIG_IO_DELAY_TYPE_0X80;
	else if (!strcmp(s, "0xed"))
		io_delay_type = CONFIG_IO_DELAY_TYPE_0XED;
	else if (!strcmp(s, "udelay"))
		io_delay_type = CONFIG_IO_DELAY_TYPE_UDELAY;
	else if (!strcmp(s, "none"))
		io_delay_type = CONFIG_IO_DELAY_TYPE_NONE;
	else
		return -EINVAL;

	io_delay_override = 1;
	return 0;
}

early_param("io_delay", io_delay_param);
