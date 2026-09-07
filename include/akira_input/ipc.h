/*
 * akira-input IPC contract.
 *
 * CANONICAL SOURCE. The overlay repository vendors a copy of this header; this
 * file is the single source of truth. Rules for changing it:
 *
 *   - Append fields only. Never reorder, never resize, never repurpose.
 *   - Bump AKIRA_INPUT_IPC_API_VERSION on any change to a struct or command.
 *   - Clients MUST refuse to proceed on version mismatch, not warn. A stale
 *     client reading a changed struct is silent corruption, not a glitch.
 */

#ifndef AKIRA_INPUT_IPC_H
#define AKIRA_INPUT_IPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AKIRA_INPUT_IPC_SERVICE_NAME "akira:i"
#define AKIRA_INPUT_IPC_API_VERSION  20u

/* Module 11 (Sf_Hipc) descriptions, matching the range the old stats service used. */
#define AKIRA_INPUT_ERR_BAD_REQUEST  0x1966B /* MAKERESULT(11, 403) - malformed/short buffer */
#define AKIRA_INPUT_ERR_UNAVAILABLE  0x1A66B /* MAKERESULT(11, 419) - backend not available */
#define AKIRA_INPUT_ERR_NO_STATE     0x1AE6B /* MAKERESULT(11, 423) - no state for that pad */
#define AKIRA_INPUT_ERR_NO_PROFILE   0x1B26B /* MAKERESULT(11, 425) - pad has not been trained */
#define AKIRA_INPUT_ERR_FULL         0x1B66B /* MAKERESULT(11, 427) - no room for another profile */

typedef enum {
    AkiraInputCmd_GetApiVersion   = 0,
    AkiraInputCmd_GetStatus       = 1,
    AkiraInputCmd_Subscribe       = 2,
    AkiraInputCmd_Unsubscribe     = 3,
    AkiraInputCmd_GetTriggerState = 4, /* in: AkiraInputTriggerQuery */
    AkiraInputCmd_GetRawReport    = 5, /* in: AkiraInputTriggerQuery */
    AkiraInputCmd_ListDevices     = 6, /* out: AkiraInputDeviceList */
    /* 7-10 were the trigger profile commands. A profile answered "where does
     * this unknown pad keep its trigger bytes"; with the supported set fixed at
     * DualSense and DualSense Edge the offsets are constants and the question
     * cannot be asked. The numbers are left as a gap rather than reused. */
    AkiraInputCmd_WriteOutputReport = 13,/* in: AkiraInputPadRef + send buffer */
    AkiraInputCmd_ProbeAudio        = 14,/* in: AkiraInputPadRef, out: AkiraInputAudioProbe */
    AkiraInputCmd_ProbeDirectWrite  = 15,/* in: AkiraInputDirectProbe + send buffer, out: uint32_t */
    AkiraInputCmd_GetDescriptor     = 16,/* in: AkiraInputPadRef, out: AkiraInputDescriptor */
    AkiraInputCmd_ZeroRetransmit    = 17,/* in: AkiraInputZeroRetransmit */
    AkiraInputCmd_ExternalControl   = 18,/* in: AkiraInputExternalControl, out: u32 rc */
    AkiraInputCmd_GetDirectOutput   = 19,/* out: uint8_t, nonzero when enabled */
    AkiraInputCmd_SetDirectOutput   = 20,/* in: uint8_t, zero disables */
} AkiraInputCmd;

/*
 * cmd 15. One write, synchronously, with the log committed on both sides of it.
 *
 * Every direct write so far has been refused with 2113-6100, and twice the
 * console has frozen hard enough to need the power button. Those two facts have
 * never been separated: the writes were going out at a haptic stream's rate, so
 * there was no way to tell whether the operation is unsafe or merely the volume
 * of it.
 *
 * This command issues exactly one, on the caller's IPC thread rather than
 * through the writer queue, with a line committed to disk before it and after.
 * If the console dies, the log ends at BEGIN and the operation itself is the
 * answer. If it returns, the rc is the answer and the console is still there to
 * read it.
 *
 * The method selects which transport to try, because cmd 19 being refused says
 * nothing about the other three.
 */
typedef struct {
    uint8_t  bt_addr[6];
    uint8_t  method;               /* AkiraInputDirectMethod */
    uint8_t  reserved;
} AkiraInputDirectProbe; /* 8 */

/*
 * cmd 15 out. Both halves of the question in one answer.
 *
 * init_rc is InitializeBluetoothDriver from when the session was opened, which
 * is the other thing worth knowing and is otherwise only visible by reading
 * the boot log over FTP. Carrying it here means one button press reports both
 * whether the driver accepted us and whether it accepted the write.
 */
typedef struct {
    uint32_t rc;                   /* the write itself */
    uint32_t init_rc;              /* InitializeBluetoothDriver, at Open() */
} AkiraInputDirectResult; /* 8 */

/*
 * Three doors into the same pad.
 *
 * Command 19 is the one that works, and it delivers a 78-byte 0x31 - proven by
 * the lightbar, by the trigger mechanism engaging, and by the pad's own report
 * changing. It does not appear to deliver anything else: 77 and 79 bytes both
 * vanish, as do the 142-byte and 547-byte audio reports, all returning success.
 *
 * Command 20 does not work and is not to be retried. It was first blamed on
 * our framing - it takes a u16 length then data, and had been handed raw report
 * bytes - but correcting that changed nothing: bluetooth.a still died on the
 * first write with an instruction abort at 0x32b9a62d10, inside its own stack
 * region of 0x32b9a5f000-0x32b9a63000. Executing an address on your own stack
 * is a smashed return address, and it smashes it on a 144-byte transfer
 * whatever the contents. Two attempts, two stack corruptions in Nintendo's
 * Bluetooth process. The command is left in the enum only so nobody spends
 * another evening rediscovering it.
 *
 * Command 21 has never been tried. It is a SET_REPORT transaction on the HID
 * control channel rather than a write on the interrupt channel - a different
 * channel with a different MTU, and the one every implementation uses for
 * feature reports.
 */
typedef enum {
    AkiraInputDirect_WriteHidData  = 0, /* btdrv cmd 19, interrupt channel */
    AkiraInputDirect_WriteHidData2 = 1, /* btdrv cmd 20 - CRASHES bluetooth.a, do not use */
    AkiraInputDirect_SetReport     = 2, /* btdrv cmd 21, control channel */
    AkiraInputDirect_MethodCount   = 3,
} AkiraInputDirectMethod;

/*
 *
 *
 */
typedef struct {
    uint8_t bt_addr[6];
    uint8_t reserved[2];
} AkiraInputPadRef; /* 8 */

/* Flags on AkiraInputDeviceInfo. Bit 1 was HasProfile. */
enum {
    AkiraInputDevice_Identified  = 1u << 0, /* vid/pid known, from MissionControl */
    AkiraInputDevice_Reporting   = 1u << 2, /* has sent a report recently; only a claimed pad can */
    AkiraInputDevice_Active      = 1u << 3, /* the pad clients get when they ask for ANY */
    AkiraInputDevice_Claimed     = 1u << 4, /* under external control right now */
};

typedef struct {
    uint8_t  bt_addr[6];
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  report_id;
    uint8_t  flags;
    uint8_t  reserved[4];
    uint64_t last_report_ns;
} AkiraInputDeviceInfo; /* 24 */

/*
 * Six, and eight, because the whole reply has to fit the raw-data response
 * window - this server emits no buffer descriptors. See the static_asserts in
 * the sysmodule, which tie both to that limit so a later field cannot silently
 * overflow it.
 */
#define AKIRA_INPUT_MAX_LISTED_DEVICES  6

typedef struct {
    uint8_t              count;
    uint8_t              reserved[7];
    AkiraInputDeviceInfo devices[AKIRA_INPUT_MAX_LISTED_DEVICES];
} AkiraInputDeviceList; /* 152 */

/*
 * cmd 13. Bytes to put on the wire to a pad, unaltered.
 *
 * The address travels as raw request data and the report itself as a map-alias
 * send buffer, because the raw window is the 0x100-byte TLS message and a
 * DualSense haptic frame is 547 bytes. Splitting one across two calls would
 * put a seam in the middle of a waveform, so the buffer is the honest shape.
 *
 * The sysmodule composes nothing. It holds a btdrv session and knows which
 * addresses are live; what a DualSense wants to hear is Akira's business, and
 * keeping it there means another PlayStation pad is a change to one header in
 * Akira rather than a change to the IPC contract and all three artifacts.
 *
 * The address must be one the sysmodule has seen a report from. That is what
 * stops this being a general "write arbitrary Bluetooth data" primitive, which
 * is not something a service should offer to anyone who can open it.
 */
#define AKIRA_INPUT_OUTPUT_MAX 560

/*
 * cmd 14 out-buffer. Whether this console can open an audio channel to a pad.
 *
 * The DualSense's haptic actuators are fed by an audio stream, not by the HID
 * output report - the report carries only routing and volume. btdrv exposes a
 * PCM path to an arbitrary paired address, so the question is simply whether
 * the Switch's Bluetooth stack will negotiate one with a controller rather
 * than a pair of headphones.
 *
 * Every result code is reported rather than the first failure, because which
 * step fails is the whole answer: refusing the connection means the route does
 * not exist, while opening it and then declining to describe a codec means it
 * does and we asked wrongly.
 */
typedef struct {
    uint32_t rc_open_connection;
    uint32_t rc_event_wait;   /* did the link finish coming up */
    uint32_t rc_open_out;
    uint32_t rc_codec;
    uint32_t rc_parameter;
    uint32_t event_type;      /* BtdrvAudioEventType; 1 is Connection */
    uint32_t audio_handle;
    uint32_t codec;           /* BtdrvAudioCodec; 0 is raw PCM */
    uint32_t pcm_channels;    /* 0 mono, non-zero stereo */
    int32_t  pcm_sample_rate;
    uint32_t pcm_bits_per_sample;
    uint32_t reserved;
} AkiraInputAudioProbe; /* 48 */

/*
 * The pad's own HID report descriptor, as btdrv holds it.
 *
 * set:sys reports zero bytes for a DualSense while btdrv reports 128, so
 * asking the wrong source is what made this look absent. Which output
 * reports a pad declares, and at what length, decides whether a frame we
 * send is well formed or garbage - and garbage is what wedges the stack.
 */
typedef struct {
    uint16_t length;
    uint16_t stored;
    uint8_t  data[128];
    uint8_t  reserved[4];
} AkiraInputDescriptor; /* 136 */

/*
 * Report ids the link should send once and never retry.
 *
 * A report the pad does not acknowledge is retransmitted, and a retried
 * report blocks everything queued behind it. For haptic audio that trade is
 * backwards: a dropped block is an inaudible glitch, a stalled queue takes
 * the console down. btdrv takes up to five ids.
 */
typedef struct {
    uint8_t bt_addr[6];
    uint8_t count;
    uint8_t report_ids[5];
    uint8_t reserved[4];
} AkiraInputZeroRetransmit; /* 16 */

/*
 *
 * A stock one does not report an unknown command - Horizon closes the session
 * instead, and every later write on it fails 0xf601, btdrv's own commands
 * included. The sysmodule therefore finds out once at startup and refuses this
 * outright when the extension is absent, rather than asking and losing btdrv.
 */
typedef struct {
    uint8_t bt_addr[6];
    uint8_t acquire;   /* zero releases */
    uint8_t reserved;
} AkiraInputExternalControl; /* 8 */

/*
 * The master switch, cmd 19 and 20.
 *
 * Off means akira never claims a pad and never writes to one: MissionControl
 * keeps translating it exactly as it does every other controller, and akira
 * drives rumble through the HOS vibration API. It is one switch for the console
 * rather than one per pad, because it exists to hand everything back at once -
 * a per-pad preference already lives in akira's own settings.
 *
 * Kept here rather than in akira so that the overlay can reach it while a
 * stream is running, and so a claim cannot outlive the answer. Turning it off
 * releases whatever is currently claimed rather than waiting to be asked.
 */

typedef struct {
    uint8_t bt_addr[6];
    uint8_t reserved[2];
} AkiraInputTriggerQuery;

/* Backend lifecycle. Failed is terminal for the process lifetime - the backend
 * never retries on its own, per the one-way-failure rule. */
enum {
    AkiraInputBackend_Unavailable = 0, /* MissionControl absent or older than v0.13.0 */
    AkiraInputBackend_Ready       = 1, /* MC present and usable, nothing claimed */
    AkiraInputBackend_Active      = 2, /* a client is subscribed; claims can be taken */
    AkiraInputBackend_Failed      = 3, /* disabled after a runtime error; will not recover */
};

/* Analog capability is asymmetric on purpose: intermediate values prove Analog,
 * but only ever seeing 0 and max does NOT prove Digital - the user may simply be
 * pressing an analog trigger all the way. Unknown never demotes to Digital. */
enum {
    AkiraInputTrigger_Unknown = 0,
    AkiraInputTrigger_Digital = 1,
    AkiraInputTrigger_Analog  = 2,
};

/* cmd 1 out-buffer. */
typedef struct {
    uint32_t api_version;
    uint32_t mc_version;        /* MissionControl GetVersion, 0 when absent */
    uint8_t  backend_state;
    uint8_t  subscriber_count;
    uint8_t  reserved0[2];
    /* Vendor and product of the most recent controller we saw report traffic
     * from but did not recognise, packed vid<<16|pid, or 0 if there has been
     * none. Turns "your pad is unsupported" into the two numbers needed to add
     * it, without any logging from a process that has nowhere to log. */
    uint32_t last_unknown_vid_pid;
    uint64_t reports_received;
    uint64_t claims_held;
    uint64_t last_error;        /* Result of whatever moved the backend to Failed */

    /*
     * Output reports that reached the pad, and ones btdrv refused.
     *
     * Reported because the client cannot otherwise tell: submitting a report
     * hands it to a writer thread and returns, so a caller that trusted its own
     * return value would believe a stream was playing while every frame of it
     * was being rejected. That is precisely what "no rumble at all" looked
     * like - a path claiming the work and silently dropping it.
     */
    uint64_t output_written;
    uint64_t output_failed;
} AkiraInputStatus;

/* cmd 4 out-buffer. Raw values plus their scale rather than a float, so the wire
 * format stays exact and the client decides how to normalise. */
typedef struct {
    uint64_t timestamp_ns;  /* when this state was published; 0 = never seen */
    uint16_t l2_raw;
    uint16_t r2_raw;
    uint16_t raw_max;       /* 255 for DS4/DualSense, 1023 for Xbox */
    uint8_t  capability;    /* AkiraInputTrigger_* */
    uint8_t  reserved0;
    uint8_t  bt_addr[6];
    uint8_t  reserved[2];
} AkiraInputTriggerState;

/* Longest raw report we retain. A DualSense 0x31 is 78 bytes, but the triggers
 * and everything around them sit well inside this, and the buffer is copied
 * inside the window where MissionControl is blocked - so it is deliberately
 * bounded rather than generous. */
#define AKIRA_INPUT_RAW_MAX 80

/*
 * cmd 5 out-buffer. The raw bytes as they arrived, before any parsing.
 *
 * This exists so a pad that produces no analog, or produces nonsense, can be
 * diagnosed rather than guessed at - and so a trigger layout can eventually be
 * learned from observation instead of hardcoded. descriptor_length is reported
 * because whether HOS gives us a usable HID report descriptor is an open
 * question: the field it arrives in holds 128 bytes while the length is a u16,
 * so the struct itself admits it can be truncated.
 */
typedef struct {
    uint64_t timestamp_ns;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t descriptor_length;  /* as HOS reports it; may exceed what it stores */
    uint8_t  reserved0;
    uint8_t  report_id;          /* data[0], repeated for convenience */
    uint8_t  length;             /* valid bytes in data */
    uint8_t  reserved;
    uint8_t  bt_addr[6];
    uint8_t  data[AKIRA_INPUT_RAW_MAX];
} AkiraInputRawReport;

/* Wire format is load-bearing across two repos - pin it. */
#ifdef __cplusplus
static_assert(sizeof(AkiraInputStatus) == 56, "AkiraInputStatus size changed - bump API version");
static_assert(sizeof(AkiraInputTriggerState) == 24, "AkiraInputTriggerState size changed - bump API version");
static_assert(sizeof(AkiraInputTriggerQuery) == 8, "AkiraInputTriggerQuery size changed - bump API version");
static_assert(sizeof(AkiraInputRawReport) == 104, "AkiraInputRawReport size changed - bump API version");
static_assert(sizeof(AkiraInputDeviceInfo) == 24, "AkiraInputDeviceInfo size changed - bump API version");
static_assert(sizeof(AkiraInputDeviceList) == 152, "AkiraInputDeviceList size changed - bump API version");
static_assert(sizeof(AkiraInputPadRef) == 8, "AkiraInputPadRef size changed - bump API version");
static_assert(sizeof(AkiraInputAudioProbe) == 48, "AkiraInputAudioProbe size changed - bump API version");
static_assert(sizeof(AkiraInputDescriptor) == 136, "AkiraInputDescriptor size changed - bump API version");
static_assert(sizeof(AkiraInputExternalControl) == 8, "AkiraInputExternalControl size changed - bump API version");
static_assert(sizeof(AkiraInputZeroRetransmit) == 16, "AkiraInputZeroRetransmit size changed - bump API version");
static_assert(sizeof(AkiraInputDirectProbe) == 8, "AkiraInputDirectProbe size changed - bump API version");
static_assert(sizeof(AkiraInputDirectResult) == 8, "AkiraInputDirectResult size changed - bump API version");
#else
_Static_assert(sizeof(AkiraInputStatus) == 56, "AkiraInputStatus size changed - bump API version");
_Static_assert(sizeof(AkiraInputTriggerState) == 24, "AkiraInputTriggerState size changed - bump API version");
_Static_assert(sizeof(AkiraInputTriggerQuery) == 8, "AkiraInputTriggerQuery size changed - bump API version");
_Static_assert(sizeof(AkiraInputRawReport) == 104, "AkiraInputRawReport size changed - bump API version");
_Static_assert(sizeof(AkiraInputAudioProbe) == 48, "AkiraInputAudioProbe size changed - bump API version");
_Static_assert(sizeof(AkiraInputDescriptor) == 136, "AkiraInputDescriptor size changed - bump API version");
_Static_assert(sizeof(AkiraInputExternalControl) == 8, "AkiraInputExternalControl size changed - bump API version");
_Static_assert(sizeof(AkiraInputZeroRetransmit) == 16, "AkiraInputZeroRetransmit size changed - bump API version");
#endif

#ifdef __cplusplus
}
#endif

#endif /* AKIRA_INPUT_IPC_H */
