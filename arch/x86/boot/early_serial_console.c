#include "boot.h"

#define DEFAULT_SERIAL_PORT 0x3f8 /* ttyS0 */

#define XMTRDY          0x20

#define DLAB		0x80

#define TXR             0       /*  Transmit register (WRITE) */
#define RXR             0       /*  Receive register  (READ)  */
#define IER             1       /*  Interrupt Enable          */
#define IIR             2       /*  Interrupt ID              */
#define FCR             2       /*  FIFO control              */
#define LCR             3       /*  Line control              */
#define MCR             4       /*  Modem control             */
#define LSR             5       /*  Line Status               */
#define MSR             6       /*  Modem Status              */
#define DLL             0       /*  Divisor Latch Low         */
#define DLH             1       /*  Divisor latch High        */

#define DEFAULT_BAUD 9600

/**
 @brief		Serial 통신을 위한 초기화를 행한다.
 */
static void
early_serial_init(
		int port, 	///< Base Port
		int baud	///< Baud rate
		)
{
	unsigned char c;
	unsigned divisor;

	outb(0x3, port + LCR);	/* 8n1 */
	outb(0, port + IER);	/* no interrupt */ // 인터럽트 금지
	outb(0, port + FCR);	/* no fifo */ // fifo 초기화
	outb(0x3, port + MCR);	/* DTR + RTS */ // data to request, request to send

	divisor	= 115200 / baud;
	c = inb(port + LCR);
	outb(c | DLAB, port + LCR);
	outb(divisor & 0xff, port + DLL);
	outb((divisor >> 8) & 0xff, port + DLH);
	outb(c & ~DLAB, port + LCR);

	early_serial_base = port;
}

/**
 @brief	command line 에 설정된 옵션 중, earlyprintk 옵션의 설정 여부를 확인하고,
	 설정되어 있다면 설정값으로 serial Port를 초기화한다.
 */
static void parse_earlyprintk(void)
{
	int baud = DEFAULT_BAUD; // 9600
	char arg[32];
	int pos = 0;
	int port = 0;
 // earlyprintk= 뒤에 오는 값이 arg에 들어간다.
	if (cmdline_find_option("earlyprintk", arg, sizeof arg) > 0) {
		char *e;

		if (!strncmp(arg, "serial", 6)) {
			port = DEFAULT_SERIAL_PORT;
			pos += 6;
		}

		if (arg[pos] == ',')
			pos++;

		/*
		 * make sure we have
		 *	"serial,0x3f8,115200"
		 *	"serial,ttyS0,115200"
		 *	"ttyS0,115200"
		 */
		if (pos == 7 && !strncmp(arg + pos, "0x", 2)) {
			port = simple_strtoull(arg + pos, &e, 16);
			if (port == 0 || arg + pos == e) // 값이 없거나 port가 0이면 Default 포트 세팅
				port = DEFAULT_SERIAL_PORT;
			else
				pos = e - arg; // 다음 검사할 문자 arg+pos
		} else if (!strncmp(arg + pos, "ttyS", 4)) {
			static const int bases[] = { 0x3f8, 0x2f8 }; // ttyS0 ttyS1
			int idx = 0;

			if (!strncmp(arg + pos, "ttyS", 4))
				pos += 4;

			if (arg[pos++] == '1')
				idx = 1;

			port = bases[idx];
		}

		if (arg[pos] == ',')
			pos++;

		baud = simple_strtoull(arg + pos, &e, 0); // baud rate를 long long으로 변환
		if (baud == 0 || arg + pos == e)
			baud = DEFAULT_BAUD;
	}

	if (port)
		early_serial_init(port, baud);
}

#define BASE_BAUD (1843200/16)
static unsigned int probe_baud(int port)
{
	unsigned char lcr, dll, dlh;
	unsigned int quot;

	lcr = inb(port + LCR);
	outb(lcr | DLAB, port + LCR);
	dll = inb(port + DLL);
	dlh = inb(port + DLH);
	outb(lcr, port + LCR);
	quot = (dlh << 8) | dll;

	return BASE_BAUD / quot;
}


/**
 @brief	command line option 중, console 옵션의 설정 여부를 확인하고\n
	 설정되어 있다면 설정 값으로 Serial port 를 초기화 한다.
 */
static void
parse_console_uart8250(void)
{
	char optstr[64], *options;
	int baud = DEFAULT_BAUD;
	int port = 0;

	/*
	 * console=uart8250,io,0x3f8,115200n8
	 * need to make sure it is last one console !
	 */
	if (cmdline_find_option("console", optstr, sizeof optstr) <= 0)
		return;

	options = optstr;

	if (!strncmp(options, "uart8250,io,", 12))
		port = simple_strtoull(options + 12, &options, 0);
	else if (!strncmp(options, "uart,io,", 8))
		port = simple_strtoull(options + 8, &options, 0);
	else
		return;

	if (options && (options[0] == ','))
		baud = simple_strtoull(options + 1, &options, 0);
	else
		baud = probe_baud(port);

	if (port)
		early_serial_init(port, baud);
}

/**
 @brief	Console 메시지 출력을 위한 Serial Port 초기화.
	 earlyprintk 옵션과 console 옵션의 설정 여부를 확인하여 설정한다.
 */
void console_init(void)
{
	parse_earlyprintk();

	if (!early_serial_base)
		parse_console_uart8250();
}
