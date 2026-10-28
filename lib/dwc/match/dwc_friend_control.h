// The friend control block (dwc_friend.c), shared with dwc_main.c on PC, where pointers make it
// larger than the 0x60 bytes dwc_main.c keeps for it
#ifndef DWC_FRIEND_CONTROL_H
#define DWC_FRIEND_CONTROL_H

#include <dwc/dwc_friend.h>
#include <dwc/dwc_datastorage.h>
#include <gamespy/GP/gp.h>
#include <revolution/os/OSTime.h>

typedef struct DWCFriendControl
{
    DWCFriendState state;
    GPConnection* pGpObj;
    u32 gpProcessCount;
    OSTime lastGpProcess;
    int friendListLen;
    DWCFriendData* friendList;
    u8 buddyUpdateIdx;
    u8 friendListChanged;
    u8 buddyUpdateState;
    u8 svUpdateComplete;
    u32 persCallbackLevel;
    int profileID;
    const u16* playerName;
    DWCUpdateServersCallback updateCallback;
    void* updateParam;
    DWCFriendStatusCallback statusCallback;
    void* statusParam;
    DWCDeleteFriendListCallback deleteCallback;
    void* deleteParam;
    DWCBuddyFriendCallback buddyCallback;
    void* buddyParam;
    DWCStorageLoginCallback persLoginCallback;
    void* persLoginParam;
    DWCSaveToServerCallback saveCallback;
    DWCLoadFromServerCallback loadCallback;
} DWCFriendControl;

#endif
