// The match control block and its types (dwc_match.c), shared with dwc_main.c on PC, where
// pointers make it larger than the Wii-sized view dwc_main.c keeps
#ifndef DWC_MATCH_CONTROL_H
#define DWC_MATCH_CONTROL_H

#include <dwc/dwc_match.h>
#include <dwc/dwc_transport.h>
#include <gamespy/qr2/qr2.h>
#include <gamespy/serverbrowsing/sb_serverbrowsing.h>
#include <revolution/os/OSTime.h>

enum
{
    DWC_MATCH_CANCEL_STATE_INIT = 0,
    DWC_MATCH_CANCEL_STATE_EXEC,
    DWC_MATCH_CANCEL_STATE_NUM
};

enum
{
    DWC_MATCH_CLOSE_STATE_INIT = 0,
    DWC_MATCH_CLOSE_STATE_CHANGE_TO_CLIENT,
    DWC_MATCH_CLOSE_STATE_TIMEOUT,
    DWC_MATCH_CLOSE_STATE_FORCED,
    DWC_MATCH_CLOSE_STATE_NUM
};

typedef enum DWCMatchPpConnectionType
{
    DWC_PP_CONNECTION_SV_CONNECT = 0,
    DWC_PP_CONNECTION_CL_GT2_CONNECT,
    DWC_PP_CONNECTION_CL_GT2_ACCEPT,
    DWC_PP_CONNECTION_SYN_FINISH,
    DWC_PP_CONNECTION_NUM
} DWCMatchPpConnectionType;

typedef enum
{
    DWC_MATCH_RESET_ALL = 0,
    DWC_MATCH_RESET_RESTART,
    DWC_MATCH_RESET_CONTINUE,
    DWC_MATCH_RESET_NUM
} DWCMatchResetLevel;

typedef enum
{
    DWC_TOPOLOGY_TYPE_HYBRID = 0,
    DWC_TOPOLOGY_TYPE_STAR = 1,
    DWC_TOPOLOGY_TYPE_FULLMESH = 2,
    DWC_TOPOLOGY_TYPE_NUM = 3
} DWCTopologyType;

// The control block keeps callbacks and their parameters as words: u32 on the Wii, pointer-sized on PC
#if TARGET_PC
typedef uintptr_t DWCiPtrWord;
#else
typedef u32 DWCiPtrWord;
#endif

// A connection's param holds the peer's profile ID, which dwc_main.c reads as a u32 (its
// DWCConnectionInfo): a pointer on the Wii, the same size there, but not on PC
#if TARGET_PC
typedef u32 DWCiConnParam;
#else
typedef void* DWCiConnParam;
#endif

typedef struct DWCstNNInfo
{
    u8 isQR2;
    u8 retryCount;
    u16 port;
    u32 ip;
    int cookie;
} DWCNNInfo;

typedef struct DWCstConnectionInfo
{
    u8 index;
    u8 aid;
    u16 reserve;
#if TARGET_PC
    union {
        DWCiConnParam param;
        u32 profileID; // dwc_main.c's name for it
    };
#else
    DWCiConnParam param;
#endif
} DWCConnectionInfo;

typedef struct DWCstMatchCommandControl
{
    u8 command;
    u8 count;
    u16 port;
    u32 ip;
    u32 data[32];
    int profileID;
    int len;
    OSTime sendTime;
} DWCMatchCommandControl;

typedef struct DWCstNodeInfoList
{
    int count;                                 // 0x000
    u8 _04[4];                                 // 0x004
    DWCNodeInfo nodes[DWC_MAX_CONNECTIONS];    // 0x008
} DWCNodeInfoList;

typedef struct DWCstMatchControl
{
    GPConnection* pGpObj;                // 0x000
    GT2Socket* pGt2Socket;               // 0x004
    GT2ConnectionCallbacks* gt2Callbacks; // 0x008
    u8 gt2ConnectCount;                  // 0x00C
    u8 connectedOnceBefore;                 // 0x00D
    u8 gt2NumValidConn;                  // 0x00E
    u8 pad1;                             // 0x00F
    qr2_t qr2Obj;                        // 0x010
    vu8 qr2MatchType;                // 0x014
    vu8 qr2MatchTypeExt;                    // 0x015
    u8 hostState;                              // 0x016
    vu8 qr2NumEntry;                     // 0x017
    vu8 qr2IsReserved;                   // 0x018
    u8 pad19;                  // 0x019
    u8 pad2[2];                          // 0x01A
    u32 suspendMatchBitmap;                             // 0x01C
    OSTime suspendRequestTick; // 20
    u32 suspendMatch;                             // 0x028
    u8 suspend;                              // 0x02C
    u8 suspendServerResponse;                              // 0x02D
    u8 pad2E[2];                         // 0x02E
    DWCiPtrWord suspendCallback;                             // 0x030
    DWCiPtrWord suspendResultParam;                             // 0x034
    OSTime suspendTimeoutTick; // 38
    u32 suspendRetry;                             // 0x040
    u32 isSuspending;                             // 0x044
    u8 qr2ShutdownFlag;                              // 0x048
    u8 pad49[7];                         // 0x049
    DWCNodeInfoList nodeInfoList;        // 0x050
    DWCNodeInfo tempNewNodeInfo;         // 0x658
    DWCNodeInfo tempNewClientInfo;                    // 0x688
    int inMeshMaking;                  // 0x6B8
    u32 lastStateBeforeStopMeshMaking;                            // 0x6BC
    DWCNodeInfo tempNodeInfoBeforeStopMeshMaking;                    // 0x6C0
    u16 qr2Port;                         // 0x6F0
    u8 pad6F2[2];                        // 0x6F2
    u32 qr2IP;                           // 0x6F4
    volatile int qr2Reservation;         // 0x6F8
    ServerBrowser sbObj;                 // 0x6FC
    int sbUpdateFlag;                    // 0x700
    u8 _704[4];                          // 0x704
    s64 sbUpdateTick;                    // 0x708
    s64 sbUpdateRequestTick;             // 0x710
    u8 nnRecvCount;                      // 0x718
    u8 nnFailureCount;                   // 0x719
    u16 nnCookieRand;                    // 0x71A
    int nnLastCookie;                    // 0x71C
    s64 nnFailedTime;                    // 0x720
    s64 nnFinishTime;                    // 0x728
    DWCNNInfo nnInfo;                    // 0x730
    DWCMatchState state;                 // 0x73C
    u8 clLinkProgress;                   // 0x740
    u8 friendCount;                      // 0x741
    u8 distantFriend;                    // 0x742
    u8 resvWaitCount;                    // 0x743
    u8 closeState;                       // 0x744
    u8 cancelState;                      // 0x745
    u8 scResvRetryCount;                 // 0x746
    u8 synResendCount;                   // 0x747
    u8 cancelSynResendCount;             // 0x748
    u8 clWaitTimeoutCount;               // 0x749
    u8 needServerPoll;                       // 0x74A
    u8 pad3;                             // 0x74B
    u16 baseLatency;                     // 0x74C
    u16 cancelBaseLatency;               // 0x74E
    u32 nnResvCheckValue;                        // 0x750
    int cmdResendFlag;                   // 0x754
    s64 cmdResendTick;                   // 0x758
    u32 cmdTimeoutTime;                  // 0x760
    u8 _764[4];                          // 0x764
    s64 cmdTimeoutStartTick;             // 0x768
    u32 synAckBit;                       // 0x770
    u32 cancelSynAckBit;                 // 0x774
    s64 lastSynSent;                     // 0x778
    s64 lastCancelSynSent;               // 0x780
    s64 closedTime;                      // 0x788
    s64 clWaitTime;                      // 0x790
    volatile int profileID;              // 0x798
    int reqProfileID;                    // 0x79C
    int cbEventPid;                      // 0x7A0
    const char* gameName;                // 0x7A4
    const char* secretKey;               // 0x7A8
    const DWCFriendData* friendList;     // 0x7AC
    int friendListLen;                   // 0x7B0
    u8 friendIdxList[DWC_MAX_MATCH_IDX_LIST]; // 0x7B4
    int friendIdxListLen;                // 0x7F4
    DWCMatchCommandControl cmdCnt;       // 0x7F8
    DWCMatchedSCCallback matchedCallback; // 0x890
    void* matchedParam;                  // 0x894
    DWCNewClientCallback newClientCallback; // 0x898
    void* newClientParam;                // 0x89C
    DWCiPtrWord evalCallback;                            // 0x8A0
    DWCiPtrWord evalParam;                            // 0x8A4
    u32 serverDownBuff;                            // 0x8A8
    u32 serverDownBase;                            // 0x8AC
    u32 groupID;                            // 0x8B0
    u8 searchGroupIDRetryHostCount;                             // 0x8B4
    u8 connectGroupID;                             // 0x8B5
    u8 serverDownQueryCount;                             // 0x8B6
    u8 serverDownNakCount;                             // 0x8B7
    OSTime serverPollingTime; // 8B8
    u8 serverFind;                             // 0x8C0
    u8 pad8C1[3];                        // 0x8C1
    DWCTopologyType topologyType;                            // 0x8C4
    u8 denyReason;                             // 0x8C8
    u8 pad8C9[3];                        // 0x8C9
    DWCEvalPlayerCallback connectAttemptCallback;  // 0x8CC
    u32 _8D0;                            // 0x8D0
    void* connectAttemptParam;                     // 0x8D4
    u32 firstServer;                            // 0x8D8
    u32 serverPollingUID;                            // 0x8DC
    OSTime suspendWaitTime; // 8E0
    u32 serverLocked;                            // 0x8E8
    u8 pad8EC[4];                        // 0x8EC
} DWCMatchControl; // size 0x8F0

#endif
