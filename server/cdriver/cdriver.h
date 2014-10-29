#ifndef _FIRMWARE_H
#define _FIRMWARE_H

#include "configuration.h"
#include <math.h>
#include <stdarg.h>
#include <stdint.h>

#define ID_SIZE 8	// Number of bytes in printerid.

#define MAXLONG (int32_t((uint32_t(1) << 31) - 1))
#define MAXINT MAXLONG

// Exactly one file defines EXTERN as empty, which leads to the data to be defined.
#ifndef EXTERN
#define EXTERN extern
#endif

struct Pin_t {
	uint8_t flags;
	uint8_t pin;
	bool valid() { return flags & 1; }
	bool inverted() { return flags & 2; }
	uint16_t write() { return flags << 8 | pin; }
	void init() { flags = 0; pin = 0; }
	inline void read(uint16_t data);
};

union ReadFloat {
	float f;
	int32_t i;
	uint32_t ui;
	uint8_t b[sizeof(float)];
};

enum SingleByteCommands {	// See serial.cpp for computation of command values.
// These bytes (except RESET) are sent in reply to a received packet only.
	CMD_NACK = 0x80,	// Incorrect packet; please resend.
	CMD_ACK0 = 0xb3,	// Packet properly received and accepted; ready for next command.  Reply follows if it should.
	CMD_ACKWAIT0 = 0xb4,	// Packet properly received and accepted, but queue is full so no new GOTO commands are allowed until CONTINUE.  (Never sent from host.)
	CMD_STALL0 = 0x87,	// Packet properly received, but not accepted; don't resend packet unmodified.
	CMD_STALL1 = 0x9e,	// Packet properly received, but not accepted; don't resend packet unmodified.
	CMD_ACKWAIT1 = 0x99,	// Printer started and is ready for commands.
	CMD_ID = 0xaa,		// Request/reply printer ID code.
	CMD_ACK1 = 0xad,	// Packet properly received and accepted; ready for next command.  Reply follows if it should.
	CMD_DEBUG = 0x81	// Debug message; a nul-terminated message follows (no checksum; no resend).
};

enum Command {
	// from host
	CMD_BEGIN,	// 4 byte: 0 (preferred protocol version). Reply: START.
	CMD_PING,	// 1 byte: code.  Reply: PONG.
	CMD_RESET,	// 1 byte: 0.
	CMD_GOTO,	// 1-2 byte: which channels (depending on number of extruders); channel * 4 byte: values [fraction/s], [mm].
	CMD_GOTOCB,	// same.  Reply (later): MOVECB.
	CMD_SLEEP,	// 1 byte: which channel (b0-6); on/off (b7 = 1/0).
	CMD_SETTEMP,	// 1 byte: which channel; 4 bytes: target [°C].
	CMD_WAITTEMP,	// 1 byte: which channel; 4 bytes: lower limit; 4 bytes: upper limit [°C].  Reply (later): TEMPCB.  Disable with WAITTEMP (NAN, NAN).
	CMD_READTEMP,	// 1 byte: which channel.  Reply: TEMP. [°C]
	CMD_READPOWER,	// 1 byte: which channel.  Reply: POWER. [μs, μs]
	CMD_SETPOS,	// 1 byte: which channel; 4 bytes: pos.
	CMD_GETPOS,	// 1 byte: which channel.  Reply: POS. [steps, mm]
	CMD_LOAD,	// 1 byte: which channel.
	CMD_SAVE,	// 1 byte: which channel.
	CMD_READ_GLOBALS,
	CMD_WRITE_GLOBALS,
	CMD_READ_SPACE_INFO,	// 1 byte: which channel.  Reply: DATA.
	CMD_READ_SPACE_AXIS,	// 1 byte: which channel.  Reply: DATA.
	CMD_READ_SPACE_MOTOR,	// 1 byte: which channel; n bytes: data.
	CMD_WRITE_SPACE_INFO,	// 1 byte: which channel.  Reply: DATA.
	CMD_WRITE_SPACE_AXIS,	// 1 byte: which channel; n bytes: data.
	CMD_WRITE_SPACE_MOTOR,	// 1 byte: which channel; n bytes: data.
	CMD_READ_TEMP,	// 1 byte: which channel.  Reply: DATA.
	CMD_WRITE_TEMP,	// 1 byte: which channel; n bytes: data.
	CMD_READ_GPIO,	// 1 byte: which channel.  Reply: DATA.
	CMD_WRITE_GPIO,	// 1 byte: which channel; n bytes: data.
	CMD_QUEUED,	// 1 byte: 0: query queue length; 1: stop and query queue length.  Reply: QUEUE.
	CMD_READPIN,	// 1 byte: which channel. Reply: GPIO.
	CMD_AUDIO_SETUP,	// 1-2 byte: which channels (like for goto); 2 byte: μs_per_sample.
	CMD_AUDIO_DATA,	// AUDIO_FRAGMENT_SIZE bytes: data.  Returns ACK or ACKWAIT.
	// to host
		// responses to host requests; only one active at a time.
	CMD_START,	// 4 byte: 0 (protocol version).
	CMD_TEMP,	// 4 byte: requested channel's temperature. [°C]
	CMD_POWER,	// 4 byte: requested channel's power time; 4 bytes: current time. [μs, μs]
	CMD_POS,	// 4 byte: pos [steps]; 4 byte: current [mm].
	CMD_DATA,	// n byte: requested data.
	CMD_PONG,	// 1 byte: PING argument.
	CMD_PIN,	// 1 byte: 0 or 1: pin state.
	CMD_QUEUE,	// 1 byte: current number of records in queue.
		// asynchronous events.
	CMD_MOVECB,	// 1 byte: number of movecb events.
	CMD_TEMPCB,	// 1 byte: which channel.  Byte storage for which needs to be sent.
	CMD_CONTINUE,	// 1 byte: is_audio.  Bool flag if it needs to be sent.
	CMD_LIMIT,	// 1 byte: which channel.
	CMD_AUTOSLEEP,	// 1 byte: what: 1: motor; 2: temp; 3: both.
	CMD_SENSE,	// 1 byte: which channel (b0-6); new state (b7); 4 byte: motor position at trigger.
};

// All temperatures are stored in Kelvin, but communicated in °C.
struct Temp
{
	// See temp.c from definition of calibration constants.
	float R0, R1, logRc, beta, Tc;	// calibration values of thermistor.  [Ω, Ω, logΩ, K, K]
	/*
	// Temperature balance calibration.
	float power;			// added power while heater is on.  [W]
	float core_C;			// heat capacity of the core.  [J/K]
	float shell_C;		// heat capacity of the shell.  [J/K]
	float transfer;		// heat transfer between core and shell.  [W/K]
	float radiation;		// radiated power = radiation * (shell_T ** 4 - room_T ** 4) [W/K**4]
	float convection;		// convected power = convection * (shell_T - room_T) [W/K]
	*/
	// Pins.
	Pin_t power_pin;
	Pin_t thermistor_pin;
	// Volatile variables.
	float target;			// target temperature; NAN to disable. [K]
	int32_t adctarget;		// target temperature in adc counts; -1 for disabled. [adccounts]
	int32_t adclast;		// last measured temperature. [adccounts]
	/*
	float core_T, shell_T;	// current temperatures. [K]
	*/
	uint8_t following_gpios;	// linked list of gpios monitoring this temp.
	float min_alarm;		// NAN, or the temperature at which to trigger the callback.  [K]
	float max_alarm;		// NAN, or the temperature at which to trigger the callback.  [K]
	int32_t adcmin_alarm;		// -1, or the temperature at which to trigger the callback.  [adccounts]
	int32_t adcmax_alarm;		// -1, or the temperature at which to trigger the callback.  [adccounts]
	bool alarm;
	// Internal variables.
	uint32_t last_temp_time;	// last value of micros when this heater was handled.
	uint32_t time_on;		// Time that the heater has been on since last reading.  [μs]
	bool is_on;			// If the heater is currently on.
	float K;			// Thermistor constant; kept in memory for performance.
	// Functions.
	int32_t get_value();		// Get thermistor reading, or -1 if it isn't available yet.
	float fromadc(int32_t adc);	// convert ADC to K.
	int32_t toadc(float T);	// convert K to ADC.
	void load(int32_t &addr, bool eeprom);
	void save(int32_t &addr, bool eeprom);
	void init();
	void free();
	void copy(Temp &dst);
	static int32_t savesize0();
	int32_t savesize() { return savesize0(); }
};

struct Axis
{
	float offset;		// Position where axis claims to be when it is at 0.
	float park;		// Park position; not used by the firmware, but stored for use by the host.
	uint8_t park_order;
	float source, current;	// Source position of current movement of axis (in μm), or current position if there is no movement.
	float max_v;
	float min, max;
	float dist, next_dist, main_dist;
	float target;
};

struct Motor
{
	Pin_t step_pin;
	Pin_t dir_pin;
	Pin_t enable_pin;
	float steps_per_m;			// hardware calibration [steps/m].
	uint8_t max_steps;			// maximum number of steps in one iteration.
	Pin_t limit_min_pin;
	Pin_t limit_max_pin;
	float home_pos;	// Position of motor (in μm) when the home switch is triggered.
	Pin_t sense_pin;
	uint8_t sense_state;
	float sense_pos;
	float limits_pos;	// position when limit switch was hit or nan
	float limit_v, limit_a;		// maximum value for f [m/s], [m/s^2].
	uint8_t home_order;
	float last_v;		// v during last iteration, for using limit_a [m/s].
	float target_v, target_dist;	// Internal values for moving.
	int32_t current_pos;	// Current position of motor (in steps).
	float endpos;
#ifdef HAVE_AUDIO
	uint8_t audio_flags;
	enum Flags {
		PLAYING = 1,
		STATE = 2
	};
#endif
};

struct Space;

struct SpaceType
{
	void (*xyz2motors)(Space *s, float *motors, bool *ok);
	void (*reset_pos)(Space *s);
	void (*check_position)(Space *s, float *data);
	void (*load)(Space *s, uint8_t old_type, int32_t &addr, bool eeprom);
	void (*save)(Space *s, int32_t &addr, bool eeprom);
	bool (*init)(Space *s);
	void (*free)(Space *s);
	int32_t (*savesize)(Space *s);
	bool (*change0)(Space *s);
};

struct Space
{
	uint8_t type;
	void *type_data;
	float max_deviation;
	uint8_t num_axes, num_motors;
	Motor **motor;
	Axis **axis;
	bool active;
	void load_info(int32_t &addr, bool eeprom);
	void load_axis(uint8_t a, int32_t &addr, bool eeprom);
	void load_motor(uint8_t m, int32_t &addr, bool eeprom);
	void save_info(int32_t &addr, bool eeprom);
	void save_axis(uint8_t a, int32_t &addr, bool eeprom);
	void save_motor(uint8_t m, int32_t &addr, bool eeprom);
	void init();
	void free();
	void copy(Space &dst);
	static int32_t savesize0();
	int32_t savesize();
	bool setup_nums(uint8_t na, uint8_t nm);
	void cancel_update();
	int32_t savesize_std();
};

// Type 0: Extruder.
void Extruder_init(uint8_t num);
#define EXTRUDER_INIT(num) Extruder_init(num);
#define HAVE_TYPE_EXTRUDER true

// Type 1: Cartesian (always available).
#define DEFAULT_TYPE 1
void Cartesian_init(uint8_t num);
#define CARTESIAN_INIT(num) Cartesian_init(num);
#define HAVE_TYPE_CARTESIAN true

// Type 2: Delta.
void Delta_init(uint8_t num);
#define DELTA_INIT(num) Delta_init(num);
#define HAVE_TYPE_DELTA true

#define NUM_SPACE_TYPES 3
EXTERN bool have_type[NUM_SPACE_TYPES];
EXTERN SpaceType space_types[NUM_SPACE_TYPES];
#define setup_spacetypes() do { \
	EXTRUDER_INIT(0) \
	have_type[0] = HAVE_TYPE_EXTRUDER; \
	CARTESIAN_INIT(1) \
	have_type[1] = HAVE_TYPE_CARTESIAN; \
	DELTA_INIT(2) \
	have_type[2] = HAVE_TYPE_DELTA; \
} while(0)
#endif

struct Gpio
{
	Pin_t pin;
	uint8_t state;
	uint8_t master;
	float value;
	int32_t adcvalue;
	uint8_t prev, next;
	void setup(uint8_t new_state);
	void load(uint8_t self, int32_t &addr, bool eeprom);
	void save(int32_t &addr, bool eeprom);
	void init();
	void free();
	void copy(Gpio &dst);
	static int32_t savesize0();
	int32_t savesize() { return savesize0(); }
};

struct MoveCommand
{
	bool cb;
	float f[2];
	float data[10];	// Value if given, NAN otherwise.  Variable size array. TODO
};

struct Serial_t {
	virtual void begin(int baud) = 0;
	virtual void write(char c) = 0;
	virtual int read() = 0;
	virtual int readBytes (char *target, int len) = 0;
	virtual void flush() = 0;
	virtual int available() = 0;
};

#define COMMAND_SIZE 127
#define COMMAND_LEN_MASK 0x7f
#define FULL_COMMAND_SIZE (COMMAND_SIZE + (COMMAND_SIZE + 2) / 3)

// Globals
EXTERN char *name;
EXTERN uint8_t namelen;
EXTERN uint8_t num_spaces;
EXTERN uint8_t num_extruders;
EXTERN uint8_t num_temps, bed_id;
EXTERN uint8_t num_gpios;
EXTERN uint32_t protocol_version;
EXTERN uint8_t printer_type;		// 0: cartesian, 1: delta.
EXTERN Pin_t led_pin, probe_pin;
EXTERN float probe_dist, probe_safe_dist;
//EXTERN float room_T;	//[°C]
EXTERN float feedrate;		// Multiplication factor for f values, used at start of move.
// Other variables.
EXTERN Serial_t *serialdev[2];
EXTERN char printerid[ID_SIZE];
EXTERN unsigned char command[2][FULL_COMMAND_SIZE];
EXTERN uint8_t command_end[2];
EXTERN char reply[FULL_COMMAND_SIZE];
EXTERN uint32_t last_current_time;
EXTERN Space *spaces;
EXTERN uint32_t next_motor_time;
EXTERN Temp *temps;
EXTERN uint32_t next_temp_time;
EXTERN Gpio *gpios;
#ifdef HAVE_AUDIO
EXTERN uint32_t next_audio_time;
#endif
EXTERN uint8_t temps_busy;
EXTERN MoveCommand queue[QUEUE_LENGTH];
EXTERN uint8_t queue_start, queue_end;
EXTERN bool queue_full;
EXTERN uint8_t num_movecbs;		// number of event notifications waiting to be sent out.
EXTERN uint8_t continue_cb;		// is a continue event waiting to be sent out? (0: no, 1: move, 2: audio, 3: both)
EXTERN uint8_t which_autosleep;		// which autosleep message to send (0: none, 1: motor, 2: temp, 3: both)
EXTERN uint8_t ping;			// bitmask of waiting ping replies.
EXTERN bool initialized;
EXTERN bool motors_busy;
EXTERN bool out_busy[2];
EXTERN uint32_t out_time[2];
EXTERN bool reply_ready;
EXTERN char pending_packet[2][FULL_COMMAND_SIZE];
EXTERN uint32_t last_active;
EXTERN float motor_limit, temp_limit;
EXTERN int16_t led_phase;
EXTERN uint8_t adc_phase;
EXTERN uint8_t temp_current;
#ifdef HAVE_AUDIO
EXTERN uint8_t audio_buffer[AUDIO_FRAGMENTS][AUDIO_FRAGMENT_SIZE];
EXTERN uint8_t audio_head, audio_tail, audio_state;
EXTERN uint32_t audio_start;
EXTERN int16_t audio_us_per_sample;
#endif
EXTERN uint32_t start_time, last_time;
EXTERN float t0, tp;
EXTERN bool moving;
EXTERN float f0, f1, f2, fp, fq, fmain;
EXTERN bool move_prepared;
EXTERN uint8_t cbs_after_current_move;
EXTERN float done_factor;
EXTERN uint8_t requested_temp;

#if DEBUG_BUFFER_LENGTH > 0
EXTERN char debug_buffer[DEBUG_BUFFER_LENGTH];
EXTERN int16_t debug_buffer_ptr;
// debug.cpp
void buffered_debug_flush();
void buffered_debug(char const *fmt, ...);
#else
#define buffered_debug debug
#define buffered_debug_flush() do {} while(0)
#endif

// packet.cpp
void packet();	// A command packet has arrived; handle it.

// serial.cpp
void serial(uint8_t which);	// Handle commands from serial.
void prepare_packet(uint8_t which, char *the_packet);
void send_packet(uint8_t which);
void try_send_next();
void write_ack(uint8_t which);
void write_ackwait(uint8_t which);
void write_stall(uint8_t which);

// move.cpp
uint8_t next_move();
void abort_move(bool move);

// setup.cpp
void setup();
void load_all();

// storage.cpp
uint8_t read_8(int32_t &address, bool eeprom);
void write_8(int32_t &address, uint8_t data, bool eeprom);
int16_t read_16(int32_t &address, bool eeprom);
void write_16(int32_t &address, int16_t data, bool eeprom);
float read_float(int32_t &address, bool eeprom);
void write_float(int32_t &address, float data, bool eeprom);

// globals.cpp
bool globals_load(int32_t &address, bool eeprom);
void globals_save(int32_t &address, bool eeprom);
int32_t globals_savesize();

// base.cpp
void reset();
#define mem_alloc(s, t, d) _mem_alloc((s), reinterpret_cast <void **>(t))
void _mem_alloc(uint32_t size, void **target);
#define mem_retarget(t1, t2) _mem_retarget(reinterpret_cast <void **>(t1), reinterpret_cast <void **>(t2))
void _mem_retarget(void **target, void **newtarget);
#define _mem_dump() do {} while(0)
#define mem_free(t) _mem_free(reinterpret_cast <void **>(t))
void _mem_free(void **target);

#include ARCH_INCLUDE

void Pin_t::read(uint16_t data) {
	if ((data & 0xff) != pin)
		SET_INPUT_NOPULLUP(*this);
	pin = data & 0xff;
	flags = data >> 8;
	if (flags & ~3 || pin >= NUM_DIGITAL_PINS + NUM_ANALOG_INPUTS) {
		flags = 0;
		pin = 0;
	}
}

struct RealSerial : public Serial_t {
	void begin(int baud) {
		Serial.begin(baud);
	}
	void write(char c) {
		Serial.write(c);
	}
	int read() {
		return Serial.read();
	}
	int readBytes (char *target, int len) {
		return Serial.readBytes(target, len);
	}
	void flush() {
		Serial.flush();
	}
	int available() {
		return Serial.available();
	}
};

EXTERN RealSerial real_serial;
