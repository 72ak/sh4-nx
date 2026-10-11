/* DirectShow, as much of it as SH4's movie player touches: a filter graph that plays nothing.
 *
 * The game (src 0x413880) builds its FMV graph by hand: CoCreateInstance for the filter graph, the MPEG-1
 * splitter, the MPEG audio and video decoders and the DirectSound renderer, AddSourceFilter for the .pac, then
 * finds the filters' pins by direction and name (EnumPins, QueryDirection, QueryId) and ConnectDirect-s them,
 * the video decoder into a renderer filter of its own (strmbase's CBaseVideoRenderer, compiled into the exe)
 * that copies frames into a D3D texture. Then IMediaControl::Run, and every frame IMediaEvent::GetEvent until
 * EC_COMPLETE, IMediaPosition::get_CurrentPosition for the fade-out, IMediaControl::Stop and Release all round.
 * None of the HRESULTs of the setup are checked, so the objects must exist; this graph hands out the pins the
 * game looks for, accepts every connection, and reports EC_COMPLETE right after Run: a movie ends at once.
 * Real playback (MPEG-1 video + MPEG audio in movie/*.pac) would hand decoded frames to the game's renderer
 * pin through IMemInputPin::Receive; this is the place for it. */
#include "../runtime/rt.h"

#define S_OK 0
#define S_FALSE 1
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_ABORT 0x80004004u
#define E_FAIL 0x80004005u
#define VFW_E_NOT_CONNECTED 0x80040209u
#define REGDB_E_CLASSNOTREG 0x80040154u
#define EC_COMPLETE 1

/* ---- the one graph: its state is global, the interface objects are views of it ---- */
static uint32_t state;         /* OAFilterState: 0 stopped, 1 paused, 2 running */
static int complete_pending;   /* EC_COMPLETE queued by Run, taken by GetEvent */
static double position;        /* seconds */

static uint32_t guid_data1(uint32_t iid) { return iid ? MEM32(iid) : 0; }

/* The objects are made on first use and shared: the game creates one player per movie and Releases
 * everything after, so a fixed set of singletons with reference counts that never reach zero is enough. */
static uint32_t vt_graph, vt_control, vt_event, vt_audio, vt_position, vt_seeking, vt_mfilter, vt_filter,
    vt_pin, vt_enum;
static uint32_t obj_graph, obj_control, obj_event, obj_audio, obj_position, obj_seeking, obj_mfilter;

static uint32_t graph_interface(uint32_t iid, uint32_t out) {
    uint32_t o = 0;
    switch (guid_data1(iid)) {
    case 0x00000000u: case 0x56A868A9u: case 0x56A868A6u: o = obj_graph; break;    /* IUnknown, IGraphBuilder, IFilterGraph */
    case 0x56A868B1u: o = obj_control; break;                                      /* IMediaControl */
    case 0x56A868B6u: case 0x56A868C0u: o = obj_event; break;                      /* IMediaEvent, IMediaEventEx */
    case 0x56A868B3u: o = obj_audio; break;                                        /* IBasicAudio */
    case 0x56A868B2u: o = obj_position; break;                                     /* IMediaPosition */
    case 0x36B73880u: o = obj_seeking; break;                                      /* IMediaSeeking */
    case 0x56A86899u: o = obj_mfilter; break;                                      /* IMediaFilter */
    }
    if (!o) rt_log("dshow: graph has no interface %08X", guid_data1(iid));
    MEM32(out) = o;
    return o ? S_OK : E_NOINTERFACE;
}

/* ---- shared IUnknown ---- */
METHOD(g_QueryInterface, 3) { return graph_interface(ARG(1), ARG(2)); }
METHOD(u_AddRef, 1) { return 2; }
METHOD(u_Release, 1) { return 1; }
METHOD(self_QueryInterface, 3) { MEM32(ARG(2)) = THIS; return S_OK; }
METHOD(ok_1, 1) { return S_OK; }
METHOD(ok_2, 2) { return S_OK; }
METHOD(ok_3, 3) { return S_OK; }
METHOD(ok_4, 4) { return S_OK; }
METHOD(ok_6, 6) { return S_OK; }
METHOD(notimpl_2, 2) { return E_NOTIMPL; }
METHOD(notimpl_3, 3) { return E_NOTIMPL; }
METHOD(notimpl_4, 4) { return E_NOTIMPL; }
METHOD(out_zero_2, 2) { MEM32(ARG(1)) = 0; return S_OK; }  /* an out pointer or long set to 0 */
METHOD(out_zero64_2, 2) { MEM32(ARG(1)) = 0; MEM32(ARG(1) + 4) = 0; return S_OK; }

/* ---- filters and pins: every filter has an input pin and "Video" / "Audio" output pins ----
 * Filter: vtable, pins[3]. Pin: vtable, filter, direction, name index. Enumerator: vtable, filter, next. */
static const char *const pin_names[3] = {"In", "Video", "Audio"};
#define PIN_FILTER(p) MEM32((p) + 4)
#define PIN_DIR(p) MEM32((p) + 8)
#define PIN_NAME(p) MEM32((p) + 12)
#define FILTER_PIN(f, i) MEM32((f) + 4 + 4 * (i))
#define ENUM_FILTER(e) MEM32((e) + 4)
#define ENUM_NEXT(e) MEM32((e) + 8)

static uint32_t new_filter(void) {
    uint32_t f = com_new(vt_filter, 16);
    for (int i = 0; i < 3; i++) {
        uint32_t p = com_new(vt_pin, 16);
        PIN_FILTER(p) = f;
        PIN_DIR(p) = i > 0;  /* PINDIR_INPUT 0, PINDIR_OUTPUT 1 */
        PIN_NAME(p) = i;
        FILTER_PIN(f, i) = p;
    }
    return f;
}

static uint32_t wide_string(const char *s) {  /* CoTaskMemAlloc'd, as QueryId must return (the game frees it) */
    uint32_t n = strlen(s), w = galloc(2 * n + 2);
    for (uint32_t i = 0; i <= n; i++) *(uint16_t *)GPTR(w + 2 * i) = (uint8_t)s[i];
    return w;
}

METHOD(pin_ConnectedTo, 2) { MEM32(ARG(1)) = 0; return VFW_E_NOT_CONNECTED; }
/* PIN_INFO: IBaseFilter *pFilter, PIN_DIRECTION dir, WCHAR achName[128]. */
METHOD(pin_QueryPinInfo, 2) {
    uint32_t info = ARG(1);
    MEM32(info) = PIN_FILTER(THIS);
    MEM32(info + 4) = PIN_DIR(THIS);
    const char *name = pin_names[PIN_NAME(THIS) % 3];
    for (uint32_t i = 0; i <= strlen(name); i++) *(uint16_t *)GPTR(info + 8 + 2 * i) = (uint8_t)name[i];
    return S_OK;
}
METHOD(pin_QueryDirection, 2) { MEM32(ARG(1)) = PIN_DIR(THIS); return S_OK; }
METHOD(pin_QueryId, 2) { MEM32(ARG(1)) = wide_string(pin_names[PIN_NAME(THIS) % 3]); return S_OK; }
static const ComEntry pin_vt[] = {
    {"IPin::QueryInterface", self_QueryInterface}, {"IPin::AddRef", u_AddRef}, {"IPin::Release", u_Release},
    {"IPin::Connect", ok_3}, {"IPin::ReceiveConnection", ok_3}, {"IPin::Disconnect", ok_1},
    {"IPin::ConnectedTo", pin_ConnectedTo}, {"IPin::ConnectionMediaType", notimpl_2},
    {"IPin::QueryPinInfo", pin_QueryPinInfo}, {"IPin::QueryDirection", pin_QueryDirection},
    {"IPin::QueryId", pin_QueryId}, {"IPin::QueryAccept", ok_2}, {"IPin::EnumMediaTypes", notimpl_2},
    {"IPin::QueryInternalConnections", notimpl_3}, {"IPin::EndOfStream", ok_1}, {"IPin::BeginFlush", ok_1},
    {"IPin::EndFlush", ok_1}, {"IPin::NewSegment", ok_6},
};

/* IEnumPins::Next(cPins, IPin **ppPins, ULONG *pcFetched): S_OK while there are pins, S_FALSE after. */
METHOD(enum_Next, 4) {
    uint32_t want = ARG(1), out = ARG(2), got = 0, f = ENUM_FILTER(THIS);
    while (got < want && ENUM_NEXT(THIS) < 3) MEM32(out + 4 * got++) = FILTER_PIN(f, ENUM_NEXT(THIS)++);
    if (ARG(3)) MEM32(ARG(3)) = got;
    return got == want ? S_OK : S_FALSE;
}
METHOD(enum_Skip, 2) { ENUM_NEXT(THIS) += ARG(1); return S_OK; }
METHOD(enum_Reset, 1) { ENUM_NEXT(THIS) = 0; return S_OK; }
METHOD(enum_Clone, 2) {
    uint32_t e = com_new(vt_enum, 12);
    ENUM_FILTER(e) = ENUM_FILTER(THIS);
    ENUM_NEXT(e) = ENUM_NEXT(THIS);
    MEM32(ARG(1)) = e;
    return S_OK;
}
static const ComEntry enum_vt[] = {
    {"IEnumPins::QueryInterface", self_QueryInterface}, {"IEnumPins::AddRef", u_AddRef},
    {"IEnumPins::Release", u_Release}, {"IEnumPins::Next", enum_Next}, {"IEnumPins::Skip", enum_Skip},
    {"IEnumPins::Reset", enum_Reset}, {"IEnumPins::Clone", enum_Clone},
};

METHOD(filter_GetState, 3) { MEM32(ARG(2)) = state; return S_OK; }
METHOD(filter_EnumPins, 2) {
    uint32_t e = com_new(vt_enum, 12);
    ENUM_FILTER(e) = THIS;
    MEM32(ARG(1)) = e;
    return S_OK;
}
METHOD(filter_FindPin, 3) {  /* by id, the name */
    char id[16] = "";
    for (int i = 0; i < 15 && *(uint16_t *)GPTR(ARG(1) + 2 * i); i++) id[i] = (char)*(uint16_t *)GPTR(ARG(1) + 2 * i);
    for (int i = 0; i < 3; i++)
        if (!strcmp(id, pin_names[i])) { MEM32(ARG(2)) = FILTER_PIN(THIS, i); return S_OK; }
    MEM32(ARG(2)) = 0;
    return E_FAIL;
}
/* FILTER_INFO: WCHAR achName[128], IFilterGraph *pGraph. */
METHOD(filter_QueryFilterInfo, 2) {
    memset(GPTR(ARG(1)), 0, 260);
    MEM32(ARG(1) + 256) = obj_graph;
    return S_OK;
}
static const ComEntry filter_vt[] = {
    {"IBaseFilter::QueryInterface", self_QueryInterface}, {"IBaseFilter::AddRef", u_AddRef},
    {"IBaseFilter::Release", u_Release}, {"IBaseFilter::GetClassID", ok_2}, {"IBaseFilter::Stop", ok_1},
    {"IBaseFilter::Pause", ok_1}, {"IBaseFilter::Run", ok_2}, {"IBaseFilter::GetState", filter_GetState},
    {"IBaseFilter::SetSyncSource", ok_2}, {"IBaseFilter::GetSyncSource", out_zero_2},
    {"IBaseFilter::EnumPins", filter_EnumPins}, {"IBaseFilter::FindPin", filter_FindPin},
    {"IBaseFilter::QueryFilterInfo", filter_QueryFilterInfo}, {"IBaseFilter::JoinFilterGraph", ok_3},
    {"IBaseFilter::QueryVendorInfo", notimpl_2},
};

/* ---- IGraphBuilder ---- */
METHOD(graph_AddFilter, 3) {
    if (rt_trace) rt_log("  AddFilter(%08X)", ARG(1));
    return S_OK;
}
METHOD(graph_ConnectDirect, 4) {
    if (rt_trace) rt_log("  ConnectDirect(%08X -> %08X)", ARG(1), ARG(2));
    return S_OK;
}
METHOD(graph_AddSourceFilter, 4) {  /* (LPCWSTR file, LPCWSTR name, IBaseFilter **out) */
    char file[260] = "";
    for (int i = 0; i < 259 && *(uint16_t *)GPTR(ARG(1) + 2 * i); i++) file[i] = (char)*(uint16_t *)GPTR(ARG(1) + 2 * i);
    rt_log("movie: %s (skipped: no DirectShow decoders yet)", file);
    position = 0;
    MEM32(ARG(3)) = new_filter();
    return S_OK;
}
static const ComEntry graph_vt[] = {
    {"IGraphBuilder::QueryInterface", g_QueryInterface}, {"IGraphBuilder::AddRef", u_AddRef},
    {"IGraphBuilder::Release", u_Release}, {"IGraphBuilder::AddFilter", graph_AddFilter},
    {"IGraphBuilder::RemoveFilter", ok_2}, {"IGraphBuilder::EnumFilters", notimpl_2},
    {"IGraphBuilder::FindFilterByName", notimpl_3}, {"IGraphBuilder::ConnectDirect", graph_ConnectDirect},
    {"IGraphBuilder::Reconnect", ok_2}, {"IGraphBuilder::Disconnect", ok_2},
    {"IGraphBuilder::SetDefaultSyncSource", ok_1}, {"IGraphBuilder::Connect", ok_3}, {"IGraphBuilder::Render", ok_2},
    {"IGraphBuilder::RenderFile", ok_3}, {"IGraphBuilder::AddSourceFilter", graph_AddSourceFilter},
    {"IGraphBuilder::SetLogFile", ok_2}, {"IGraphBuilder::Abort", ok_1}, {"IGraphBuilder::ShouldOperationContinue", ok_1},
};

/* ---- IMediaControl (IDispatch first: 4 methods after IUnknown) ---- */
METHOD(control_Run, 1) {
    state = 2;
    complete_pending = 1;  /* nothing to play: the movie is over as soon as it starts */
    if (rt_trace) rt_log("  IMediaControl::Run");
    return S_OK;
}
METHOD(control_Pause, 1) { state = 1; return S_OK; }
METHOD(control_Stop, 1) { state = 0; complete_pending = 0; return S_OK; }
METHOD(control_GetState, 3) { MEM32(ARG(2)) = state; return S_OK; }
static const ComEntry control_vt[] = {
    {"IMediaControl::QueryInterface", g_QueryInterface}, {"IMediaControl::AddRef", u_AddRef},
    {"IMediaControl::Release", u_Release}, {"IMediaControl::GetTypeInfoCount", notimpl_2},
    {"IMediaControl::GetTypeInfo", notimpl_4}, {"IMediaControl::GetIDsOfNames", NULL},
    {"IMediaControl::Invoke", NULL}, {"IMediaControl::Run", control_Run}, {"IMediaControl::Pause", control_Pause},
    {"IMediaControl::Stop", control_Stop}, {"IMediaControl::GetState", control_GetState},
    {"IMediaControl::RenderFile", ok_2}, {"IMediaControl::AddSourceFilter", notimpl_3},
    {"IMediaControl::get_FilterCollection", notimpl_2}, {"IMediaControl::get_RegFilterCollection", notimpl_2},
    {"IMediaControl::StopWhenReady", control_Stop},
};

/* ---- IMediaEventEx ---- */
METHOD(event_GetEvent, 5) {  /* (long *code, LONG_PTR *p1, LONG_PTR *p2, long timeout): E_ABORT when none */
    if (!complete_pending) return E_ABORT;
    complete_pending = 0;
    MEM32(ARG(1)) = EC_COMPLETE;
    MEM32(ARG(2)) = S_OK;
    MEM32(ARG(3)) = 0;
    return S_OK;
}
METHOD(event_WaitForCompletion, 3) { complete_pending = 0; MEM32(ARG(2)) = EC_COMPLETE; return S_OK; }
static const ComEntry event_vt[] = {
    {"IMediaEventEx::QueryInterface", g_QueryInterface}, {"IMediaEventEx::AddRef", u_AddRef},
    {"IMediaEventEx::Release", u_Release}, {"IMediaEventEx::GetTypeInfoCount", notimpl_2},
    {"IMediaEventEx::GetTypeInfo", notimpl_4}, {"IMediaEventEx::GetIDsOfNames", NULL}, {"IMediaEventEx::Invoke", NULL},
    {"IMediaEventEx::GetEventHandle", out_zero_2}, {"IMediaEventEx::GetEvent", event_GetEvent},
    {"IMediaEventEx::WaitForCompletion", event_WaitForCompletion}, {"IMediaEventEx::CancelDefaultHandling", ok_2},
    {"IMediaEventEx::RestoreDefaultHandling", ok_2}, {"IMediaEventEx::FreeEventParams", ok_4},
    {"IMediaEventEx::SetNotifyWindow", ok_4}, {"IMediaEventEx::SetNotifyFlags", ok_2},
    {"IMediaEventEx::GetNotifyFlags", out_zero_2},
};

/* ---- IBasicAudio: volume in 1/100 dB, -10000 silent ---- */
static const ComEntry audio_vt[] = {
    {"IBasicAudio::QueryInterface", g_QueryInterface}, {"IBasicAudio::AddRef", u_AddRef},
    {"IBasicAudio::Release", u_Release}, {"IBasicAudio::GetTypeInfoCount", notimpl_2},
    {"IBasicAudio::GetTypeInfo", notimpl_4}, {"IBasicAudio::GetIDsOfNames", NULL}, {"IBasicAudio::Invoke", NULL},
    {"IBasicAudio::put_Volume", ok_2}, {"IBasicAudio::get_Volume", out_zero_2}, {"IBasicAudio::put_Balance", ok_2},
    {"IBasicAudio::get_Balance", out_zero_2},
};

/* ---- IMediaPosition: times are doubles in seconds ---- */
static void put_double(uint32_t va, double d) { memcpy(GPTR(va), &d, 8); }
METHOD(position_get_Duration, 2) { put_double(ARG(1), 0.0); return S_OK; }
METHOD(position_put_CurrentPosition, 3) { memcpy(&position, GPTR(g_esp + 8), 8); return S_OK; }  /* this, double */
METHOD(position_get_CurrentPosition, 2) { put_double(ARG(1), position); return S_OK; }
METHOD(position_get_Rate, 2) { put_double(ARG(1), 1.0); return S_OK; }
METHOD(position_put_double, 3) { return S_OK; }
static const ComEntry position_vt[] = {
    {"IMediaPosition::QueryInterface", g_QueryInterface}, {"IMediaPosition::AddRef", u_AddRef},
    {"IMediaPosition::Release", u_Release}, {"IMediaPosition::GetTypeInfoCount", notimpl_2},
    {"IMediaPosition::GetTypeInfo", notimpl_4}, {"IMediaPosition::GetIDsOfNames", NULL}, {"IMediaPosition::Invoke", NULL},
    {"IMediaPosition::get_Duration", position_get_Duration},
    {"IMediaPosition::put_CurrentPosition", position_put_CurrentPosition},
    {"IMediaPosition::get_CurrentPosition", position_get_CurrentPosition},
    {"IMediaPosition::get_StopTime", position_get_Duration}, {"IMediaPosition::put_StopTime", position_put_double},
    {"IMediaPosition::get_PrerollTime", position_get_Duration}, {"IMediaPosition::put_PrerollTime", position_put_double},
    {"IMediaPosition::put_Rate", position_put_double}, {"IMediaPosition::get_Rate", position_get_Rate},
    {"IMediaPosition::CanSeekForward", out_zero_2}, {"IMediaPosition::CanSeekBackward", out_zero_2},
};

/* ---- IMediaSeeking: times in 100 ns units (LONGLONG) ---- */
METHOD(seeking_GetPositions, 3) { MEM32(ARG(1)) = MEM32(ARG(1) + 4) = 0; MEM32(ARG(2)) = MEM32(ARG(2) + 4) = 0; return S_OK; }
METHOD(seeking_SetPositions, 5) { return S_OK; }
METHOD(seeking_ConvertTimeFormat, 5) { MEM32(ARG(1)) = MEM32(ARG(1) + 4) = 0; return S_OK; }
METHOD(seeking_GetRate, 2) { put_double(ARG(1), 1.0); return S_OK; }
METHOD(seeking_SetRate, 3) { return S_OK; }
static const ComEntry seeking_vt[] = {
    {"IMediaSeeking::QueryInterface", g_QueryInterface}, {"IMediaSeeking::AddRef", u_AddRef},
    {"IMediaSeeking::Release", u_Release}, {"IMediaSeeking::GetCapabilities", out_zero_2},
    {"IMediaSeeking::CheckCapabilities", out_zero_2}, {"IMediaSeeking::IsFormatSupported", ok_2},
    {"IMediaSeeking::QueryPreferredFormat", ok_2}, {"IMediaSeeking::GetTimeFormat", ok_2},
    {"IMediaSeeking::IsUsingTimeFormat", ok_2}, {"IMediaSeeking::SetTimeFormat", ok_2},
    {"IMediaSeeking::GetDuration", out_zero64_2}, {"IMediaSeeking::GetStopPosition", out_zero64_2},
    {"IMediaSeeking::GetCurrentPosition", out_zero64_2}, {"IMediaSeeking::ConvertTimeFormat", seeking_ConvertTimeFormat},
    {"IMediaSeeking::SetPositions", seeking_SetPositions}, {"IMediaSeeking::GetPositions", seeking_GetPositions},
    {"IMediaSeeking::GetAvailable", seeking_GetPositions}, {"IMediaSeeking::SetRate", seeking_SetRate},
    {"IMediaSeeking::GetRate", seeking_GetRate}, {"IMediaSeeking::GetPreroll", out_zero64_2},
};

/* ---- IMediaFilter ---- */
static const ComEntry mfilter_vt[] = {
    {"IMediaFilter::QueryInterface", g_QueryInterface}, {"IMediaFilter::AddRef", u_AddRef},
    {"IMediaFilter::Release", u_Release}, {"IMediaFilter::GetClassID", ok_2}, {"IMediaFilter::Stop", control_Stop},
    {"IMediaFilter::Pause", control_Pause}, {"IMediaFilter::Run", control_Run}, {"IMediaFilter::GetState", control_GetState},
    {"IMediaFilter::SetSyncSource", ok_2}, {"IMediaFilter::GetSyncSource", out_zero_2},
};

#define VT(name) com_vtable(name##_vt, sizeof name##_vt / sizeof *name##_vt)
static void dshow_init(void) {
    if (vt_graph) return;
    vt_graph = VT(graph); vt_control = VT(control); vt_event = VT(event); vt_audio = VT(audio);
    vt_position = VT(position); vt_seeking = VT(seeking); vt_mfilter = VT(mfilter);
    vt_filter = VT(filter); vt_pin = VT(pin); vt_enum = VT(enum);
    obj_graph = com_new(vt_graph, 8); obj_control = com_new(vt_control, 8); obj_event = com_new(vt_event, 8);
    obj_audio = com_new(vt_audio, 8); obj_position = com_new(vt_position, 8); obj_seeking = com_new(vt_seeking, 8);
    obj_mfilter = com_new(vt_mfilter, 8);
}

/* CoCreateInstance(clsid, outer, context, iid, out): the graph, or a filter for any of the classes the movie
 * player asks for (MPEG-1 splitter 336475D0, MPEG audio decoder 4A2286E0, MPEG video decoder FEB50740,
 * DirectSound renderer 79376820). Anything else is not registered, as on a PC without the codecs. */
WINAPI(CoCreateInstance, "CoCreateInstance", 5) {
    dshow_init();
    uint32_t clsid = guid_data1(ARG(0)), out = ARG(4);
    switch (clsid) {
    case 0xE436EBB3u: return graph_interface(ARG(3), out);  /* CLSID_FilterGraph */
    case 0x336475D0u: case 0x4A2286E0u: case 0xFEB50740u: case 0x79376820u:
        MEM32(out) = new_filter();
        return S_OK;
    }
    rt_log("CoCreateInstance(%08X-%04X): no such class", clsid, *(uint16_t *)GPTR(ARG(0) + 4));
    MEM32(out) = 0;
    return REGDB_E_CLASSNOTREG;
}
