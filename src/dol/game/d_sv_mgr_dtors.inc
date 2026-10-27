// Explicit specializations place each base destructor before its concrete step.
#if TARGET_PC
// GCC (and the standard) reject a specialization after the bases were instantiated above; the order
// only matters to match the Wii build
template <class Owner, class Step> inline dSvMgr_c::stepBase_c<Owner, Step>::~stepBase_c() {}
#endif
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepLoad_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepLoad_c::~stepLoad_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveNormal_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveNormal_c::~stepSaveNormal_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveRetireNetVst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveRetireNetVst_c::~stepSaveRetireNetVst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveConnectNetHst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveConnectNetHst_c::~stepSaveConnectNetHst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveConnectNetVst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveConnectNetVst_c::~stepSaveConnectNetVst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveContinueNetHst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveContinueNetHst_c::~stepSaveContinueNetHst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveContinueNetVst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveContinueNetVst_c::~stepSaveContinueNetVst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveInterruptNetHst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveInterruptNetHst_c::~stepSaveInterruptNetHst_c() {}
#if !TARGET_PC
template <>
inline dSvMgr_c::stepBase_c<dSvMgr_c, dSvMgr_c::stepSaveInterruptNetVst_c>::~stepBase_c() {}
#endif
inline dSvMgr_c::stepSaveInterruptNetVst_c::~stepSaveInterruptNetVst_c() {}
inline dSvMgr_c::~dSvMgr_c() {}
