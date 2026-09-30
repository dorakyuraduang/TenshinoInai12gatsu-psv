#define SDL_MAIN_HANDLED
#include "vita_hw_codec.hpp"
#include <psp2/kernel/error.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
static void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
static uint32_t ticks = 1000;
static uint32_t testTicks() {
    return ticks;
}
static std::string fail;
static bool module = false, vlib = false, vdec = false, alib = false, adec = false, noPictures = false;
static int nextUid = 1, unmapHandle = -1, inputStalls = 0;
static uint32_t physicalBudget = 26 * 1024 * 1024;
static uint32_t physicalUsed = 0;
static uint32_t virtualAddress = 0, videoInputs = 0, audioInputs = 0, videoOutputs = 0;
static std::deque<uint64_t> pts;
struct Block {
    std::unique_ptr<uint8_t[]> data;
    uint32_t size;
    SceKernelMemBlockType type;
};
static std::map<int, Block> blocks;
static int result(const char *name) {
    return fail == name ? int32_t(0x80620005u) : 0;
}
static void (*musicHook)(void *, Uint8 *, int) = nullptr;
static void *musicUser = nullptr;
static void Mix_HookMusic(void (*hook)(void *, Uint8 *, int), void *user) {
    musicHook = hook;
    musicUser = user;
}
static void Mix_HaltMusic() {}
static void Mix_HaltChannel(int) {}
extern "C" {
int sceSysmoduleIsLoaded(SceSysmoduleModuleId) {
    return module ? 0 : -1;
}
int sceSysmoduleLoadModule(SceSysmoduleModuleId) {
    if (result("module"))
        return result("module");
    module = true;
    return 0;
}
int sceSysmoduleUnloadModule(SceSysmoduleModuleId) {
    require(!vdec && !vlib && !adec && !alib && blocks.empty() && unmapHandle < 0 && !virtualAddress,
            "Module unloaded before codec resources");
    module = false;
    return 0;
}
// SDL's Vita texture pool has already reserved all CDRAM, as on the reported device.
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info) {
    info->size_user = 64 * 1024 * 1024;
    info->size_cdram = 0;
    info->size_phycont = physicalBudget - physicalUsed;
    return 0;
}
SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size,
                              SceKernelAllocMemBlockOpt *opt) {
    if (result(name))
        return result(name);
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
        return int32_t(SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE_CDRAM);
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW) {
        require(size % (1024 * 1024) == 0 && !opt, "Wrong PHYCONT allocation contract");
        if (size > physicalBudget - physicalUsed)
            return int32_t(SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE);
        physicalUsed += size;
    } else {
        require(type == SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE && size % 4096 == 0,
                "Wrong audio memory type/granularity");
    }
    int id = nextUid++;
    blocks.emplace(id, Block{std::make_unique<uint8_t[]>(size), size, type});
    return id;
}
int sceKernelGetMemBlockBase(SceUID id, void **base) {
    if (result("base"))
        return result("base");
    *base = blocks.at(id).data.get();
    return 0;
}
int sceKernelFreeMemBlock(SceUID id) {
    const auto &block = blocks.at(id);
    if (block.type == SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW)
        physicalUsed -= block.size;
    require(blocks.erase(id) == 1, "Duplicate memory free");
    return 0;
}
void sceVideodecSetConfigInternal(SceVideodecType, int cfg) {
    require(cfg == 2, "Wrong codec configuration");
}
void sceAvcdecSetDecodeMode(SceVideodecType, int mode) {
    require(mode == 0x80, "Wrong AVC mode");
}
int sceVideodecQueryMemSizeInternal(SceVideodecType, SceVideodecQueryInitInfo *q, uint32_t *n) {
    require(q->hwAvc.horizontal == 800 && q->hwAvc.vertical == 608 && q->hwAvc.numOfRefFrames == 2 &&
                q->hwAvc.numOfStreams == 1,
            "Wrong SPS geometry/reference count");
    *n = fail == "query-context" ? 0 : 5767168; // Actual 0.22 device query
    return result("query-context-api");
}
int sceAvcdecQueryDecoderMemSizeInternal(SceVideodecType, SceAvcdecQueryDecoderInfo *,
                                         SceAvcdecDecoderInfo *d) {
    d->frameMemSize = 5505024; // Actual 0.22 device query
    return result("query-frames");
}
SceUID sceCodecEngineOpenUnmapMemBlock(void *, SceSize n) {
    if (result("unmap"))
        return result("unmap");
    require(n % 0x100000 == 0, "Codec context block not MB aligned");
    unmapHandle = 99;
    return unmapHandle;
}
int sceCodecEngineCloseUnmapMemBlock(SceUID) {
    require(!vlib && !virtualAddress, "Unmap closed before codec context");
    unmapHandle = -1;
    return 0;
}
SceUIntVAddr sceCodecEngineAllocMemoryFromUnmapMemBlock(SceUID, uint32_t, uint32_t alignment) {
    require(alignment == 0x40000, "Codec address alignment mismatch");
    virtualAddress = fail == "address" ? 0 : 0x40000;
    return virtualAddress;
}
int sceCodecEngineFreeMemoryFromUnmapMemBlock(SceUID, SceUIntVAddr) {
    require(!vlib, "Codec address freed with library alive");
    virtualAddress = 0;
    return 0;
}
int sceVideodecInitLibraryWithUnmapMemInternal(SceVideodecType, SceVideodecCtrl *,
                                               SceVideodecQueryInitInfo *) {
    if (result("video-library"))
        return result("video-library");
    vlib = true;
    return 0;
}
int sceVideodecTermLibrary(SceVideodecType) {
    require(!vdec, "AVC library terminated before decoder");
    vlib = false;
    return 0;
}
int sceAvcdecCreateDecoderInternal(SceVideodecType, SceAvcdecCtrl *c, SceAvcdecQueryDecoderInfo *) {
    if (result("video-decoder"))
        return result("video-decoder");
    require(c->frameBuf.pBuf && c->frameBuf.size, "Missing reference storage");
    vdec = true;
    videoInputs = videoOutputs = 0;
    pts.clear();
    return 0;
}
int sceAvcdecDeleteDecoder(SceAvcdecCtrl *) {
    require(!musicHook, "Stopped codec before detaching audio callback");
    vdec = false;
    pts.clear();
    return 0;
}
int sceAvcdecDecodeAvailableSize(SceAvcdecCtrl *) {
    if (inputStalls > 0) {
        --inputStalls;
        return 0;
    }
    return result("available") ? result("available") : 1024 * 1024;
}
int sceAvcdecDecodeAuInternal(SceAvcdecCtrl *, const SceAvcdecAu *au, int *pic) {
    if (result("video-input"))
        return result("video-input");
    if (au->es.size) {
        auto *p = static_cast<uint8_t *>(au->es.pBuf);
        require(au->es.size > 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1,
                "Missing Annex B start code");
        if (!videoInputs)
            require((p[4] & 31) == 7, "First access unit missing SPS");
        pts.push_back((uint64_t(au->pts.upper) << 32) | au->pts.lower);
        ++videoInputs;
    }
    *pic = 1;
    return 0;
}
int sceAvcdecDecodeGetPictureWithWorkPictureInternal(SceAvcdecCtrl *, SceAvcdecArrayPicture *out,
                                                     SceAvcdecArrayPicture *, int *) {
    if (result("video-output"))
        return result("video-output");
    if (noPictures || pts.empty())
        return 0;
    auto *p = out->pPicture[0];
    require(out->numOfElm == 1 && p->frame.pixelType == SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER,
            "Wrong NV12 output request");
    memset(p->frame.pPicture[0], 16, 800 * 608);
    memset(static_cast<uint8_t *>(p->frame.pPicture[0]) + 800 * 608, 128, 800 * 608 / 2);
    auto stamp = pts.front();
    pts.pop_front();
    p->info.pts = {uint32_t(stamp >> 32), uint32_t(stamp)};
    out->numOfOutput = 1;
    ++videoOutputs;
    return 0;
}
int sceAvcdecDecodeStop(SceAvcdecCtrl *c, SceAvcdecArrayPicture *out) {
    int pic = 0;
    return sceAvcdecDecodeGetPictureWithWorkPictureInternal(c, out, nullptr, &pic);
}
int sceAudiodecInitLibrary(uint32_t, SceAudiodecInitParam *) {
    if (result("audio-library"))
        return result("audio-library");
    alib = true;
    return 0;
}
int sceAudiodecTermLibrary(uint32_t) {
    require(!adec, "AAC library terminated before decoder");
    alib = false;
    return 0;
}
int sceAudiodecCreateDecoder(SceAudiodecCtrl *c, uint32_t) {
    if (result("audio-decoder"))
        return result("audio-decoder");
    require(c->pInfo->aac.isAdts == 0 && c->pInfo->aac.ch == 2 && c->pInfo->aac.samplingRate == 44100,
            "Wrong AAC config");
    adec = true;
    audioInputs = 0;
    return 0;
}
int sceAudiodecDeleteDecoder(SceAudiodecCtrl *) {
    require(!musicHook, "AAC freed while callback attached");
    adec = false;
    return 0;
}
int sceAudiodecDecode(SceAudiodecCtrl *c) {
    if (result("audio-input"))
        return result("audio-input");
    require(c->inputEsSize <= 1536 && c->maxPcmSize >= 4096, "Bad AAC sizes");
    memset(c->pPcm, 23, 4096);
    c->outputPcmSize = 4096;
    ++audioInputs;
    return 0;
}
}
#define SDL_GetTicks testTicks
#include "movie_vita.hpp"
#undef SDL_GetTicks
static void clean() {
    require(!module && !vlib && !vdec && !alib && !adec && !musicHook && blocks.empty() && unmapHandle < 0 &&
                !virtualAddress && !physicalUsed,
            "Leaked decoder resources");
}
int main(int argc, char **argv) {
    SDL_SetMainReady();
    SDL_Init(0);
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_RGBA32);
    auto *r = SDL_CreateSoftwareRenderer(surface);
    try {
        std::string file = argc > 1 ? argv[1] : "psv/assets/opening.mp4";
        {
            Movie m;
            m.start(r, file);
            require(physicalUsed == 13 * 1024 * 1024, "Unexpected physical decoder budget");
            std::array<uint8_t, 735 * 4> pcm{};
            bool done = false;
            for (int i = 0; i < 5600; ++i) {
                ticks += 17;
                if (!m.draw(r)) {
                    done = true;
                    break;
                }
                require(musicHook, "No first-frame audio gate");
                if (i == 120) {
                    auto pos = m.position();
                    auto inputs = videoInputs;
                    m.setPaused(true);
                    ticks += 5000;
                    m.draw(r);
                    pcm.fill(99);
                    musicHook(musicUser, pcm.data(), pcm.size());
                    require(m.position() == pos && videoInputs == inputs && pcm[0] == 0,
                            "Pause advanced media");
                    m.setPaused(false);
                }
                auto inputCount = videoInputs + audioInputs;
                musicHook(musicUser, pcm.data(), pcm.size());
                require(videoInputs + audioInputs == inputCount, "Audio callback decoded media");
            }
            require(done && videoInputs == 2695 && videoOutputs == 2695 && audioInputs == 3870,
                    "Incomplete full-length playback");
            require(m.position() == 3961724 / 44100., "AAC priming/padding was not trimmed exactly");
            require(m.uploads == 2695 && m.skippedFrames() == 0, "Normal playback lost presentations");
            m.stop();
            clean();
        }
        {
            Movie m;
            m.start(r, file);
            inputStalls = 2;
            m.draw(r);
            m.draw(r);
            require(videoInputs == 0 && !musicHook, "Backpressure consumed an access unit or released audio");
            m.draw(r);
            require(videoInputs == 1 && musicHook, "Backpressure retry lost the first access unit");
            m.stop();
            clean();
        }
        {
            // Two 6 MiB blocks fit, but the output block does not: partial init must unwind.
            physicalBudget = 12 * 1024 * 1024;
            Movie m;
            bool caught = false;
            try {
                m.start(r, file);
            } catch (const std::exception &e) {
                caught = std::string(e.what()).find("avc-nv12 failed: 0x80024302") != std::string::npos;
            }
            require(caught, "Physical-pool exhaustion did not report the real failing allocation");
            m.stop();
            clean();
            physicalBudget = 26 * 1024 * 1024;
        }
        const std::vector<std::string> failures = {
            "module",        "query-context",  "query-context-api", "query-frames",
            "avc-context",   "avc-references", "avc-nv12",          "base",
            "unmap",         "address",        "video-library",     "video-decoder",
            "audio-library", "aac-input",      "aac-pcm",           "audio-decoder",
            "available",     "video-input",    "video-output",      "audio-input"};
        for (auto &stage : failures) {
            fail = stage;
            bool caught = false;
            {
                Movie m;
                try {
                    m.start(r, file);
                    m.draw(r);
                } catch (const std::exception &) {
                    caught = true;
                }
                m.stop();
            }
            require(caught, "Injected native error ignored");
            clean();
        }
        fail.clear();
        {
            noPictures = true;
            Movie m;
            m.start(r, file);
            bool caught = false;
            try {
                for (int i = 0; i < 200; ++i) {
                    ticks += 17;
                    m.draw(r);
                    require(!musicHook, "Audio started without a video frame");
                }
            } catch (const std::exception &e) {
                caught = std::string(e.what()).find("no first frame") != std::string::npos;
            }
            require(caught, "No-picture decoder did not time out");
            m.stop();
            noPictures = false;
            clean();
        }
        {
            Movie m;
            m.start(r, file);
            m.draw(r);
            m.stop();
            clean();
        }
        SDL_DestroyRenderer(r);
        SDL_FreeSurface(surface);
        SDL_Quit();
        std::cout << "Direct hardware regression passed: SDL CDRAM exhausted, real 13 MiB PHY budget, partial PHY exhaustion, 2695 AVC + 3870 AAC packets, exact audio trim, "
                     "pause, start gate, EOF, 20 native failures, ES backpressure, no-picture timeout, early "
                     "stop, cleanup\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "REGRESSION FAILED: " << e.what() << '\n';
        return 1;
    }
}
