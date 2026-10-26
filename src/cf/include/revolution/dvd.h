#ifndef REVOLUTION_DVD_H
#define REVOLUTION_DVD_H
// PC: aurora's DVD (backed by nod), plus the RVL additions of the decomp's dvd.h.
#include <revolution/types.h>
#include <dolphin/dvd.h>

#ifdef __cplusplus
extern "C" {
#endif

// RVL command priorities
enum {
    DVD_PRIO_HIGHEST = 0,
    DVD_PRIO_HIGH,
    DVD_PRIO_MEDIUM,
    DVD_PRIO_LOW,

    DVD_PRIO_MAX,
};

// RVL names for aurora's states and results. aurora reports the GameCube values, so each RVL name
// maps to aurora's code for the same condition (DVD_RESULT_CANCELED is aurora's -6, not the RVL -3).
#define DVD_RESULT_OK DVD_RESULT_GOOD
#define DVD_RESULT_FATAL DVD_RESULT_FATAL_ERROR
#define DVD_STATE_FATAL DVD_STATE_FATAL_ERROR
#define DVD_STATE_IDLE DVD_STATE_END
#define DVD_STATE_COVER_OPENED DVD_STATE_COVER_OPEN
#define DVD_STATE_WRONG_DISK_ID DVD_STATE_WRONG_DISK
#define DVD_STATE_PAUSED DVD_STATE_PAUSING
// RVL only (11 there, which is aurora's DVD_STATE_RETRY): a value aurora never reports
#define DVD_STATE_DISK_ERROR 100

// The RVL name of aurora's DVDCBCallback (same signature)
typedef DVDCBCallback DVDCommandCallback;

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCommandCallback callback);

#ifdef __cplusplus
}
#endif

#endif // REVOLUTION_DVD_H
