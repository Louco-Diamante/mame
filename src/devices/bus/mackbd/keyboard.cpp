// license: BSD-3-Clause
// copyright-holders: Vas Crabb
/***************************************************************************

    Apple M0110/M0120 keyboard/keypad

    These peripherals have a lot in common:
    * Based on Intel or Philips mask-programmed 8021 microcontroller
    * 100µH inductor as timing element (gives approximately 3MHz)
    * Identical microcontroller programs
    * Identical external watchdog circuits based on 74LS123
    * Identical power-on/brownout reset circuits
    * Identical host communication interface via P07 and P20
    * Keys read with a mixture of matrix and dedicated inputs
    * Neither has diodes to prevent ghosting

    The program chooses keyboard or keypad mode based on whether P05
    reads low with no outputs driven low.

    The keyboard has dedicated inputs for the four modifiers (Shift,
    Command, Option, and mechanically toggling Caps Lock - no Control
    keys and no distinction between left/right Shift and Option).  The
    rest of the keys (52 for ANSI or 53 for ISO) are read via a
    nine-by-six matrix.

    The keypad has dedicated inputs for the digits and (rather
    ineffciently) reads the other eight keys via a three-by-three
    matrix.

    +-----------+---------------+-------------------+
    | Pin       | Keyboard      | Keypad            |
    +-----------+---------------+-------------------+
    | P00 (4)   | column read   | row drive         |
    | P01 (5)   | column read   | row drive         |
    | P02 (6)   | column read   | row drive         |
    | P03 (7)   | column read   | 8                 |
    | P04 (8)   | column read   | 9                 |
    | P05 (9)   | column read   | sense             |
    | P06 (10)  | Caps Lock     | keyboard clock    |
    | P07 (11)  | host clock    | host clock        |
    +-----------+---------------+-------------------+
    | P10 (18)  | row drive     | 0                 |
    | P11 (19)  | row drive     | 1                 |
    | P12 (20)  | row drive     | 2                 |
    | P13 (21)  | row drive     | 3                 |
    | P14 (22)  | row drive     | 4                 |
    | P15 (23)  | row drive     | 5                 |
    | P16 (24)  | row drive     | 6                 |
    | P17 (25)  | row drive     | 7                 |
    +-----------+---------------+-------------------+
    | P20 (26)  | host data     | host data         |
    | P21 (27)  | row drive*    | keyboard data     |
    | P22 (1)   | Shift         | column read       |
    | P23 (2)   | Command       | column read       |
    +-----------+---------------+-------------------+
    | T1 (13)   | Option        | column read       |
    +-----------+---------------+-------------------+
    * falling edge starts/resets watchdog timer

    +------+-----------------------------------------------------+
    | ANSI | P21   P10   P11   P12   P13   P14   P15   P16   P17 |
    +------+-----------------------------------------------------+
    | P00  |  A     Z     Q     1     =     ]    Rtn    \    Tab |
    | P01  |  S     X     W     2     9     O     L     ,    Spc |
    | P02  |  D     C     E     3     7     U     J     /     `  |
    | P03  |  F     V     R     4     -     [     '     N    Bsp |
    | P04  |  H           Y     6     8     I     K     M    Ent |
    | P05  |  G     B     T     5     0     P     ;     .        |
    +------+-----------------------------------------------------+

    +------+-----------------------------------------------------+
    | ISO  | P21   P10   P11   P12   P13   P14   P15   P16   P17 |
    +------+-----------------------------------------------------+
    | P00  |  A     `     Q     1     =     ]     \    Rtn   Tab |
    | P01  |  S     Z     W     2     9     O     L     M    Ent |
    | P02  |  D     X     E     3     7     U     J     .     §  |
    | P03  |  F     C     R     4     -     [     '     B    Bsp |
    | P04  |  H     /     Y     6     8     I     K     N    Spc |
    | P05  |  G     V     T     5     0     P     ;     ,        |
    +------+-----------------------------------------------------+

    +------+-----------------+
    | Pad  | P00   P01   P02 |
    +------+-----------------+
    | P22  |  /    Clr    .  |
    | P23  |  -     ,     *  |
    | T1   | Ent    +        |
    +------+-----------------+

    Known part numbers:
    * M0110 (U.S. - ANSI)
    * M0110B (British - ISO)
    * M0110F (French - ISO)
    * M0110T (Italian - ISO)
    * M0120 (keypad - English)
    * M0120P (keypad - European)

    ISO keyboards and the European keypad use icons rather than English
    text for Backspace, Tab, Caps Lock, Shift, Return, Shift, Option,
    Enter and Clear (all variants use icons for command and the cursor
    arrows).

    Also known to exist:
    * Japanese - ANSI (U.S. with different key caps)

    TODO:
    * Determine whether P00 or P01 actually feeds the keypad watchdog
    * Find more model numbers and implement more layouts

***************************************************************************/

#include "emu.h"
#include "keyboard.h"

#include "cpu/mcs48/mcs48.h"

#define LOG_GENERAL     (1U << 0)
#define LOG_WATCHDOG    (1U << 1)
#define LOG_MATRIX      (1U << 2)
#define LOG_COMM        (1U << 3)

//#define VERBOSE (LOG_GENERAL | LOG_WATCHDOG | LOG_MATRIX | LOG_COMM)
//#define LOG_OUTPUT_STREAM std::cerr
#include "logmacro.h"

#define LOGWATCHDOG(...)    LOGMASKED(LOG_WATCHDOG, __VA_ARGS__)
#define LOGMATRIX(...)      LOGMASKED(LOG_MATRIX, __VA_ARGS__)
#define LOGCOMM(...)        LOGMASKED(LOG_COMM, __VA_ARGS__)


namespace {

ROM_START(keyboard)
	ROM_REGION(0x0400, "mpu", 0)
	ROM_LOAD("ip8021h_2173.bin", 0x000000, 0x000400, CRC(5fbd9a94) SHA1(32a3b58afb445a8675b12a4de3aec2fa00c99222))
ROM_END


template <unsigned Rows>
class peripheral_base : public device_t, public device_mac_keyboard_interface
{
public:
	CUSTOM_INPUT_MEMBER(columns_r)
	{
		ioport_value result(make_bitmask<ioport_value>(Rows));
		for (unsigned i = 0U; Rows > i; ++i)
		{
			if (!BIT(m_row_drive, i))
				result &= m_rows[i]->read();
		}
		LOGMATRIX("read matrix: row drive = %X, result = %X\n", m_row_drive, result);
		return result ^ make_bitmask<ioport_value>(Rows);
	}

	CUSTOM_INPUT_MEMBER(host_data_r)
	{
		return m_host_data_in ^ 0x01;
	}

protected:
	peripheral_base(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock)
		: device_t(mconfig, type, tag, owner, clock)
		, device_mac_keyboard_interface(mconfig, *this)
		, m_mpu{ *this, "mpu" }
		, m_rows{ *this, "ROW%u", 0U }
	{
	}

	virtual tiny_rom_entry const *device_rom_region() const override
	{
		return ROM_NAME(keyboard);
	}

	virtual void device_add_mconfig(machine_config &config) override
	{
		I8021(config, m_mpu, 3'000'000); // 100µH inductor gives approximately 3MHz
		m_mpu->p0_out_cb().set(FUNC(peripheral_base::host_clock_w)).bit(7);
		m_mpu->p2_out_cb().set(FUNC(peripheral_base::host_data_w)).bit(0);
	}

	virtual void device_start() override
	{
		m_watchdog_timeout = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(peripheral_base::watchdog_timeout), this));
		m_watchdog_output = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(peripheral_base::watchdog_output), this));

		m_row_drive = make_bitmask<u16>(Rows);
		m_host_clock_out = 1U;
		m_host_data_out = 1U;
		m_host_data_in = 0x01U;
		m_watchdog_in = 1U;

		save_item(NAME(m_row_drive));
		save_item(NAME(m_host_clock_out));
		save_item(NAME(m_host_data_out));
		save_item(NAME(m_host_data_in));
		save_item(NAME(m_watchdog_in));
	}

	virtual DECLARE_WRITE_LINE_MEMBER(data_w) override
	{
		machine().scheduler().synchronize(timer_expired_delegate(FUNC(peripheral_base::update_host_data), this), state);
	}

	DECLARE_WRITE_LINE_MEMBER(watchdog_w)
	{
		if (bool(state) != bool(m_watchdog_in))
		{
			m_watchdog_in = state ? 1U : 0U;
			if (!state)
			{
				// 74LS123 with Rt = 200kΩ and Cext = 1µF
				// t = K * Rt * Cext * (1 + (0.7 / Rt)) = 0.28 * 200k * 1µ * (1 + (0.7 / 200k)) ≈ 56ms
				m_watchdog_timeout->adjust(attotime::from_msec(56));
				LOGWATCHDOG("%s: watchdog reset\n", machine().describe_context());
			}
		}
	}

	template <u16 Mask> void set_row_drive(u16 value)
	{
		m_row_drive = ((m_row_drive & ~Mask) | (value & Mask)) & make_bitmask<u16>(Rows);
	}

	required_device<i8021_device> m_mpu;

private:
	DECLARE_WRITE_LINE_MEMBER(host_clock_w)
	{
		if (bool(state) != bool(m_host_clock_out))
		{
			if (state)
				LOGCOMM("%s: host clock out 0 -> 1 data=%u\n", machine().describe_context(), (m_host_data_out && m_host_data_in) ? 1U : 0U);
			else
				LOGCOMM("%s: host clock out 1 -> 0\n", machine().describe_context());
			write_clock(m_host_clock_out = state ? 1U : 0U);
		}
	}

	DECLARE_WRITE_LINE_MEMBER(host_data_w)
	{
		if (bool(state) != bool(m_host_data_out))
		{
			LOGCOMM("%s: host data out %u -> %u\n", machine().describe_context(), m_host_data_out, state ? 1U : 0U);
			write_data(m_host_data_out = state ? 1U : 0U);
		}
	}

	TIMER_CALLBACK_MEMBER(update_host_data)
	{
		if (bool(param) != bool(m_host_data_in))
		{
			LOGCOMM("host data in %u -> %u\n", m_host_data_in, param ? 1U : 0U);
			m_host_data_in = param ? 0x01U : 0x00U;
		}
	}

	TIMER_CALLBACK_MEMBER(watchdog_timeout)
	{
		// 74LS123 with Rt = 200kΩ and Cext = 100nF
		// t = K * Rt * Cext * (1 + (0.7 / Rt)) = 0.28 * 200k * 100n * (1 + (0.7 / 200k)) ≈ 5.6ms
		m_watchdog_output->adjust(attotime::from_usec(5600));
		LOG("watchdog timeout\n");
		m_mpu->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
	}

	TIMER_CALLBACK_MEMBER(watchdog_output)
	{
		LOGWATCHDOG("watchdog release\n");
		m_mpu->set_input_line(INPUT_LINE_RESET, CLEAR_LINE);
	}

	required_ioport_array<Rows> m_rows;

	emu_timer *m_watchdog_timeout = nullptr;
	emu_timer *m_watchdog_output = nullptr;

	u16 m_row_drive         = make_bitmask<u16>(Rows);  // current bit pattern driving rows (active low)
	u8  m_host_clock_out    = 1U;                       // clock line drive to host (idle high)
	u8  m_host_data_out     = 1U;                       // data line drive to host (idle high)
	u8  m_host_data_in      = 0x01U;                    // data line drive from host (idle high)
	u8  m_watchdog_in       = 1U;                       // watchdog start/reset (falling edge trigger)
};


class keyboard_base : public peripheral_base<9>
{
protected:
	keyboard_base(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock)
		: peripheral_base<9>(mconfig, type, tag, owner, clock)
	{
	}

	virtual void device_add_mconfig(machine_config &config) override
	{
		peripheral_base<9>::device_add_mconfig(config);

		m_mpu->p0_in_cb().set_ioport("P0");
		m_mpu->p1_out_cb().set([this] (u8 data) { set_row_drive<0x01feU>(u16(data) << 1); });
		m_mpu->p2_in_cb().set_ioport("P2");
		m_mpu->p2_out_cb().append([this] (u8 data) { set_row_drive<0x0001U>((data >> 1) & 0x01U); });
		m_mpu->p2_out_cb().append(FUNC(keyboard_base::watchdog_w)).bit(1);
		m_mpu->t1_in_cb().set_ioport("T1");
	}
};


class keypad_base : public peripheral_base<3>
{
public:
	CUSTOM_INPUT_MEMBER(keyboard_clock_r)
	{
		return m_keyboard_clock_in ^ 0x01;
	}

	CUSTOM_INPUT_MEMBER(keyboard_data_r)
	{
		return m_keyboard_data_in ^ 0x01;
	}

protected:
	keypad_base(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock)
		: peripheral_base<3>(mconfig, type, tag, owner, clock)
		, m_keyboard_port(*this, "kbd")
	{
	}

	virtual void device_add_mconfig(machine_config &config) override
	{
		peripheral_base<3>::device_add_mconfig(config);

		m_mpu->p0_in_cb().set_ioport("P0");
		m_mpu->p0_out_cb().append(FUNC(keypad_base::set_row_drive<0x07U>));
		m_mpu->p0_out_cb().append(FUNC(keypad_base::watchdog_w)).bit(1); // TODO: confirm whether P0 or P1 resets watchdog
		m_mpu->p1_in_cb().set_ioport("P1");
		m_mpu->p2_in_cb().set_ioport("P2");
		m_mpu->p2_out_cb().append(FUNC(keypad_base::keyboard_data_out_w)).bit(1);
		m_mpu->t1_in_cb().set([this] () { return int((~columns_r() >> 2) & 0x01U); });

		MAC_KEYBOARD_PORT(config, m_keyboard_port, mac_keyboard_devices, "us");
		m_keyboard_port->clock_cb().set(FUNC(keypad_base::keyboard_clock_in_w));
		m_keyboard_port->data_cb().set(FUNC(keypad_base::keyboard_data_in_w));
	}

	virtual void device_start() override
	{
		peripheral_base<3>::device_start();

		m_keyboard_data_out = 0x01U;
		m_keyboard_clock_in = 0x01U;
		m_keyboard_data_in = 0x01U;

		save_item(NAME(m_keyboard_data_out));
		save_item(NAME(m_keyboard_clock_in));
		save_item(NAME(m_keyboard_data_in));
	}

	required_device<mac_keyboard_port_device> m_keyboard_port;

private:
	DECLARE_WRITE_LINE_MEMBER(keyboard_data_out_w)
	{
		if (bool(state) != bool(m_keyboard_data_out))
		{
			LOGCOMM("%s: keyboard data out %u -> %u\n", machine().describe_context(), m_keyboard_data_out, state ? 1U : 0U);
			m_keyboard_port->data_w(m_keyboard_data_out = state ? 0x01U : 0x00U);
		}
	}

	DECLARE_WRITE_LINE_MEMBER(keyboard_clock_in_w)
	{
		machine().scheduler().synchronize(timer_expired_delegate(FUNC(keypad_base::update_keyboard_clock), this), state);
	}

	DECLARE_WRITE_LINE_MEMBER(keyboard_data_in_w)
	{
		machine().scheduler().synchronize(timer_expired_delegate(FUNC(keypad_base::update_keyboard_data), this), state);
	}

	TIMER_CALLBACK_MEMBER(update_keyboard_clock)
	{
		m_keyboard_clock_in = param ? 0x01U : 0x00U;
	}

	TIMER_CALLBACK_MEMBER(update_keyboard_data)
	{
		m_keyboard_data_in = param ? 0x01U : 0x00U;
	}

	u8  m_keyboard_data_out = 0x01U;        // data line drive to keyboard (idle high)
	u8  m_keyboard_clock_in = 0x01U;        // clock line driver from keyboard (idle high)
	u8  m_keyboard_data_in  = 0x01U;        // data line drive from keyboard (idle high)
};


INPUT_PORTS_START(keyboard_us)
	PORT_START("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_A)          PORT_CHAR('a')  PORT_CHAR('A')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_S)          PORT_CHAR('s')  PORT_CHAR('S')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_D)          PORT_CHAR('d')  PORT_CHAR('D')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F)          PORT_CHAR('f')  PORT_CHAR('F')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_H)          PORT_CHAR('h')  PORT_CHAR('H')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_G)          PORT_CHAR('g')  PORT_CHAR('G')

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z)          PORT_CHAR('z')  PORT_CHAR('Z')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X)          PORT_CHAR('x')  PORT_CHAR('X')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C)          PORT_CHAR('c')  PORT_CHAR('C')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V)          PORT_CHAR('v')  PORT_CHAR('V')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B)          PORT_CHAR('b')  PORT_CHAR('B')

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q)          PORT_CHAR('q')  PORT_CHAR('Q')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W)          PORT_CHAR('w')  PORT_CHAR('W')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_E)          PORT_CHAR('e')  PORT_CHAR('E')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_R)          PORT_CHAR('r')  PORT_CHAR('R')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y)          PORT_CHAR('y')  PORT_CHAR('Y')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_T)          PORT_CHAR('t')  PORT_CHAR('T')

	PORT_START("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1)          PORT_CHAR('1')  PORT_CHAR('!')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2)          PORT_CHAR('2')  PORT_CHAR('@')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3)          PORT_CHAR('3')  PORT_CHAR('#')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4)          PORT_CHAR('4')  PORT_CHAR('$')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6)          PORT_CHAR('6')  PORT_CHAR('^')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5)          PORT_CHAR('5')  PORT_CHAR('%')

	PORT_START("ROW4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS)     PORT_CHAR('=')  PORT_CHAR('+')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9)          PORT_CHAR('9')  PORT_CHAR('(')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7)          PORT_CHAR('7')  PORT_CHAR('&')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS)      PORT_CHAR('-')  PORT_CHAR('_')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8)          PORT_CHAR('8')  PORT_CHAR('*')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0)          PORT_CHAR('0')  PORT_CHAR(')')

	PORT_START("ROW5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']')  PORT_CHAR('}')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_O)          PORT_CHAR('o')  PORT_CHAR('O')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_U)          PORT_CHAR('u')  PORT_CHAR('U')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE)  PORT_CHAR('[')  PORT_CHAR('{')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_I)          PORT_CHAR('i')  PORT_CHAR('I')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_P)          PORT_CHAR('p')  PORT_CHAR('P')

	PORT_START("ROW6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(0x0d)                PORT_NAME("Return")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_L)          PORT_CHAR('l')  PORT_CHAR('L')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_J)          PORT_CHAR('j')  PORT_CHAR('J')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE)      PORT_CHAR('\'') PORT_CHAR('"')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_K)          PORT_CHAR('k')  PORT_CHAR('K')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON)      PORT_CHAR(';')  PORT_CHAR(':')

	PORT_START("ROW7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR('\\') PORT_CHAR('|')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(',')  PORT_CHAR('<')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('/')  PORT_CHAR('?')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N)          PORT_CHAR('n')  PORT_CHAR('N')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M)          PORT_CHAR('m')  PORT_CHAR('M')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP)       PORT_CHAR('.')  PORT_CHAR('>')

	PORT_START("ROW8")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TAB)        PORT_CHAR(0x09)                PORT_NAME("Tab")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE)      PORT_CHAR(' ')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE)      PORT_CHAR('`')  PORT_CHAR('~')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSPACE)  PORT_CHAR(0x08)                PORT_NAME("Backspace")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RCONTROL)                                  PORT_NAME("Enter")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("P0")
	PORT_BIT(0x3f, IP_ACTIVE_LOW, IPT_CUSTOM) PORT_CUSTOM_MEMBER(keyboard_base, columns_r)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CAPSLOCK)                         PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK)) PORT_NAME("Caps Lock") PORT_TOGGLE
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("P2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_CUSTOM) PORT_CUSTOM_MEMBER(keyboard_base, host_data_r)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)           PORT_NAME("Shift")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LCONTROL)                                                            PORT_NAME("Command")
	PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("T1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LALT)   PORT_CODE(KEYCODE_RALT)   PORT_CHAR(UCHAR_SHIFT_2)           PORT_NAME("Option")
INPUT_PORTS_END

INPUT_PORTS_START(keyboard_gb)
	PORT_INCLUDE(keyboard_us)

	PORT_MODIFY("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH2) PORT_CHAR('\\')  PORT_CHAR('|')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z)          PORT_CHAR('z')   PORT_CHAR('Z')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X)          PORT_CHAR('x')   PORT_CHAR('X')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C)          PORT_CHAR('c')   PORT_CHAR('C')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('/')   PORT_CHAR('?')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V)          PORT_CHAR('v')   PORT_CHAR('V')

	PORT_MODIFY("ROW3")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3)          PORT_CHAR('3')   PORT_CHAR(0xa3)  // £

	PORT_MODIFY("ROW6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR('`')   PORT_CHAR('~')

	PORT_MODIFY("ROW7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(0x0d)                   PORT_NAME("Return")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M)          PORT_CHAR('m')   PORT_CHAR('M')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP)       PORT_CHAR('.')   PORT_CHAR('>')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B)          PORT_CHAR('b')   PORT_CHAR('B')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N)          PORT_CHAR('n')   PORT_CHAR('N')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(',')   PORT_CHAR('<')

	PORT_MODIFY("ROW8")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RCONTROL)                                     PORT_NAME("Enter")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE)      PORT_CHAR(0xa7)  PORT_CHAR('#')   // §
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE)      PORT_CHAR(' ')
INPUT_PORTS_END

INPUT_PORTS_START(keyboard_fr)
	PORT_INCLUDE(keyboard_us)

	PORT_MODIFY("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_A)          PORT_CHAR('q')   PORT_CHAR('Q')

	PORT_MODIFY("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH2) PORT_CHAR('<')   PORT_CHAR('>')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z)          PORT_CHAR('w')   PORT_CHAR('W')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X)          PORT_CHAR('x')   PORT_CHAR('X')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C)          PORT_CHAR('c')   PORT_CHAR('C')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH)      PORT_CHAR('=')   PORT_CHAR('+')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V)          PORT_CHAR('v')   PORT_CHAR('V')

	PORT_MODIFY("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q)          PORT_CHAR('a')   PORT_CHAR('A')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W)          PORT_CHAR('z')   PORT_CHAR('Z')

	PORT_MODIFY("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1)          PORT_CHAR('&')   PORT_CHAR('1')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2)          PORT_CHAR(0xe9)  PORT_CHAR('2')   // é
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3)          PORT_CHAR('"')   PORT_CHAR('3')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4)          PORT_CHAR('\'')  PORT_CHAR('4')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6)          PORT_CHAR(0xa7)  PORT_CHAR('6')   // §
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5)          PORT_CHAR('(')   PORT_CHAR('5')

	PORT_MODIFY("ROW4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS)     PORT_CHAR('-')   PORT_CHAR('_')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9)          PORT_CHAR(0xe7)  PORT_CHAR('9')   // ç
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7)          PORT_CHAR(0xe8)  PORT_CHAR('7')   // è
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS)      PORT_CHAR(')')   PORT_CHAR(0xb0)  // °
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8)          PORT_CHAR('!')   PORT_CHAR('8')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0)          PORT_CHAR(0xe0)  PORT_CHAR('0')   // à

	PORT_MODIFY("ROW5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('$')   PORT_CHAR('*')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE)  PORT_CHAR(0x2c6) PORT_CHAR(0xa8)  // ˆ ¨

	PORT_MODIFY("ROW6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR('`')   PORT_CHAR(0xa3)  // £
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE)      PORT_CHAR(0xf9)  PORT_CHAR('%')   // ù
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON)      PORT_CHAR('m')   PORT_CHAR('M')

	PORT_MODIFY("ROW7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(0x0d)                   PORT_NAME("Return")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M)          PORT_CHAR(',')   PORT_CHAR('?')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP)       PORT_CHAR(':')   PORT_CHAR('/')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B)          PORT_CHAR('b')   PORT_CHAR('B')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N)          PORT_CHAR('n')   PORT_CHAR('N')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(';')   PORT_CHAR('.')

	PORT_MODIFY("ROW8")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RCONTROL)                                     PORT_NAME("Enter")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE)      PORT_CHAR('@')   PORT_CHAR('#')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE)      PORT_CHAR(' ')
INPUT_PORTS_END

INPUT_PORTS_START(keyboard_it)
	PORT_INCLUDE(keyboard_us)

	PORT_MODIFY("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH2) PORT_CHAR('<')   PORT_CHAR('>')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z)          PORT_CHAR('w')   PORT_CHAR('W')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X)          PORT_CHAR('x')   PORT_CHAR('X')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C)          PORT_CHAR('c')   PORT_CHAR('C')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH)      PORT_CHAR(0xf2)  PORT_CHAR('!')   // ò
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V)          PORT_CHAR('v')   PORT_CHAR('V')

	PORT_MODIFY("ROW2")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W)          PORT_CHAR('z')   PORT_CHAR('Z')

	PORT_MODIFY("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1)          PORT_CHAR('&')   PORT_CHAR('1')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2)          PORT_CHAR('"')   PORT_CHAR('2')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3)          PORT_CHAR('\'')  PORT_CHAR('3')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4)          PORT_CHAR('(')   PORT_CHAR('4')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6)          PORT_CHAR(0xe8)  PORT_CHAR('6')   // è
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5)          PORT_CHAR(0xe7)  PORT_CHAR('5')   // ç

	PORT_MODIFY("ROW4")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9)          PORT_CHAR(0xe0)  PORT_CHAR('9')   // à
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7)          PORT_CHAR(')')   PORT_CHAR('7')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8)          PORT_CHAR(0xa3)  PORT_CHAR('8')   // £
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0)          PORT_CHAR(0xe9)  PORT_CHAR('0')   // é

	PORT_MODIFY("ROW5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('$')   PORT_CHAR('*')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE)  PORT_CHAR(0xec)  PORT_CHAR(0x2c6) // ì ˆ

	PORT_MODIFY("ROW6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH)  PORT_CHAR(0xa7)  PORT_CHAR(0xb0)  // § °
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE)      PORT_CHAR(0xf9)  PORT_CHAR('%')   // ù
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON)      PORT_CHAR('m')   PORT_CHAR('M')

	PORT_MODIFY("ROW7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER)      PORT_CHAR(0x0d)                   PORT_NAME("Return")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M)          PORT_CHAR(',')   PORT_CHAR('?')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP)       PORT_CHAR(':')   PORT_CHAR('/')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B)          PORT_CHAR('b')   PORT_CHAR('B')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N)          PORT_CHAR('n')   PORT_CHAR('N')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA)      PORT_CHAR(';')   PORT_CHAR('.')

	PORT_MODIFY("ROW8")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RCONTROL)                                     PORT_NAME("Enter")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE)      PORT_CHAR('@')   PORT_CHAR('#')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE)      PORT_CHAR(' ')
INPUT_PORTS_END


class m0110_device : public keyboard_base
{
public:
	m0110_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keyboard_base(mconfig, MACKBD_M0110, tag, owner, clock)
	{
	}

protected:
	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keyboard_us);
	}
};

class m0110b_device : public keyboard_base
{
public:
	m0110b_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keyboard_base(mconfig, MACKBD_M0110B, tag, owner, clock)
	{
	}

protected:
	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keyboard_gb);
	}
};

class m0110f_device : public keyboard_base
{
public:
	m0110f_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keyboard_base(mconfig, MACKBD_M0110F, tag, owner, clock)
	{
	}

protected:
	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keyboard_fr);
	}
};

class m0110t_device : public keyboard_base
{
public:
	m0110t_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keyboard_base(mconfig, MACKBD_M0110T, tag, owner, clock)
	{
	}

protected:
	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keyboard_it);
	}
};


INPUT_PORTS_START(keypad)
	PORT_START("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_MINUS_PAD)  PORT_CHAR(UCHAR_MAMEKEY(SLASH_PAD)) PORT_NAME("Keypad / (Up)")
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_EQUALS_PAD) PORT_CHAR(UCHAR_MAMEKEY(MINUS_PAD)) PORT_NAME("Keypad -")
	PORT_BIT(0x04, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_ENTER_PAD)  PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD)) PORT_NAME("Keypad Enter")

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_NUMLOCK)    PORT_CHAR(UCHAR_MAMEKEY(NUMLOCK))   PORT_NAME("Keypad Clear")
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_PLUS_PAD)   PORT_CHAR(UCHAR_MAMEKEY(COMMA_PAD)) PORT_NAME("Keypad , (Down)")
	PORT_BIT(0x04, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_SLASH_PAD)  PORT_CHAR(UCHAR_MAMEKEY(PLUS_PAD))  PORT_NAME("Keypad + (Left)")

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))   PORT_NAME("Keypad .")
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_ASTERISK)   PORT_CHAR(UCHAR_MAMEKEY(ASTERISK))  PORT_NAME("Keypad * (Right)")
	PORT_BIT(0x04, IP_ACTIVE_LOW,  IPT_UNUSED)

	PORT_START("P0")
	PORT_BIT(0x07, IP_ACTIVE_LOW,  IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_8_PAD)      PORT_CHAR(UCHAR_MAMEKEY(8_PAD))     PORT_NAME("Keypad 8")
	PORT_BIT(0x10, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_9_PAD)      PORT_CHAR(UCHAR_MAMEKEY(9_PAD))     PORT_NAME("Keypad 9")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_CUSTOM)
	PORT_BIT(0x40, IP_ACTIVE_LOW,  IPT_CUSTOM) PORT_CUSTOM_MEMBER(keypad_base, keyboard_clock_r)
	PORT_BIT(0x80, IP_ACTIVE_LOW,  IPT_UNUSED)

	PORT_START("P1")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_0_PAD)      PORT_CHAR(UCHAR_MAMEKEY(0_PAD))     PORT_NAME("Keypad 0")
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_1_PAD)      PORT_CHAR(UCHAR_MAMEKEY(1_PAD))     PORT_NAME("Keypad 1")
	PORT_BIT(0x04, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_2_PAD)      PORT_CHAR(UCHAR_MAMEKEY(2_PAD))     PORT_NAME("Keypad 2")
	PORT_BIT(0x08, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_3_PAD)      PORT_CHAR(UCHAR_MAMEKEY(3_PAD))     PORT_NAME("Keypad 3")
	PORT_BIT(0x10, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_4_PAD)      PORT_CHAR(UCHAR_MAMEKEY(4_PAD))     PORT_NAME("Keypad 4")
	PORT_BIT(0x20, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_5_PAD)      PORT_CHAR(UCHAR_MAMEKEY(5_PAD))     PORT_NAME("Keypad 5")
	PORT_BIT(0x40, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_6_PAD)      PORT_CHAR(UCHAR_MAMEKEY(6_PAD))     PORT_NAME("Keypad 6")
	PORT_BIT(0x80, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_7_PAD)      PORT_CHAR(UCHAR_MAMEKEY(7_PAD))     PORT_NAME("Keypad 7")

	PORT_START("P2")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_CUSTOM) PORT_CUSTOM_MEMBER(keypad_base, host_data_r)
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_CUSTOM) PORT_CUSTOM_MEMBER(keypad_base, keyboard_data_r)
	PORT_BIT(0x0c, IP_ACTIVE_LOW,  IPT_CUSTOM) PORT_CUSTOM_MEMBER(keypad_base, columns_r)
INPUT_PORTS_END

INPUT_PORTS_START(keypad_eu)
	PORT_INCLUDE(keypad)

	PORT_MODIFY("ROW1")
	PORT_BIT(0x02, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_PLUS_PAD)   PORT_CHAR(UCHAR_MAMEKEY(COMMA_PAD)) PORT_NAME("Keypad . (Down)")

	PORT_MODIFY("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW,  IPT_KEYPAD) PORT_CODE(KEYCODE_DEL_PAD)    PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))   PORT_NAME("Keypad ,")
INPUT_PORTS_END


class m0120_device : public keypad_base
{
public:
	m0120_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keypad_base(mconfig, MACKBD_M0120, tag, owner, clock)
	{
	}

protected:
	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keypad);
	}
};

class m0120p_device : public keypad_base
{
public:
	m0120p_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: keypad_base(mconfig, MACKBD_M0120P, tag, owner, clock)
	{
	}

protected:
	virtual void device_add_mconfig(machine_config &config) override
	{
		keypad_base::device_add_mconfig(config);

		m_keyboard_port->set_default_option("fr");
	}

	virtual ioport_constructor device_input_ports() const override
	{
		return INPUT_PORTS_NAME(keypad_eu);
	}
};

} // anonymous namespace


DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0110,  device_mac_keyboard_interface, m0110_device,  "mackbd_m0110",  "Macintosh Keyboard (U.S. - M0110)")
DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0110B, device_mac_keyboard_interface, m0110b_device, "mackbd_m0110b", "Macintosh Keyboard (British - M0110B)")
DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0110F, device_mac_keyboard_interface, m0110f_device, "mackbd_m0110f", "Macintosh Keyboard (French - M0110F)")
DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0110T, device_mac_keyboard_interface, m0110t_device, "mackbd_m0110t", "Macintosh Keyboard (Italian - M0110T)")

DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0120,  device_mac_keyboard_interface, m0120_device,  "mackbd_m0120",  "Macintosh Numeric Keypad (English - M0120)")
DEFINE_DEVICE_TYPE_PRIVATE(MACKBD_M0120P, device_mac_keyboard_interface, m0120p_device, "mackbd_m0120p", "Macintosh Numeric Keypad (European - M0120P)")
