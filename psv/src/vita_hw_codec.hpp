#pragma once
// Split AVC AU/picture sequence adapted from ReAvPlayer (MIT),
// Copyright (c) 2021 Jaylon Gowie. See licenses/ReAvPlayer-MIT.txt.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <psp2/audiodec.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/sysmodule.h>
#include <psp2/videodec.h>
#include <stdexcept>
#include <vector>
// Exported VitaSDK symbols absent from its public C header. API layout/signatures
// checked against the wiliwili h264_vita backend used by spyro-98/vita-hw-decoder.
struct SceVideodecCtrl {
    SceAvcdecBuf memBuf;
    SceUID memBufUid;
    SceUIntVAddr vaContext;
    SceUInt32 contextSize;
};
extern "C" {
void sceVideodecSetConfigInternal(SceVideodecType, SceInt32);
void sceAvcdecSetDecodeMode(SceVideodecType, SceInt32);
SceInt32 sceVideodecQueryMemSizeInternal(SceVideodecType, SceVideodecQueryInitInfo *, SceUInt32 *);
SceInt32 sceAvcdecQueryDecoderMemSizeInternal(SceVideodecType, SceAvcdecQueryDecoderInfo *,
                                              SceAvcdecDecoderInfo *);
SceInt32 sceVideodecInitLibraryWithUnmapMemInternal(SceVideodecType, SceVideodecCtrl *,
                                                    SceVideodecQueryInitInfo *);
SceInt32 sceAvcdecCreateDecoderInternal(SceVideodecType, SceAvcdecCtrl *, SceAvcdecQueryDecoderInfo *);
SceInt32 sceAvcdecDecodeAvailableSize(SceAvcdecCtrl *);
SceInt32 sceAvcdecDecodeAuInternal(SceAvcdecCtrl *, const SceAvcdecAu *, SceInt32 *);
SceInt32 sceAvcdecDecodeGetPictureWithWorkPictureInternal(SceAvcdecCtrl *, SceAvcdecArrayPicture *,
                                                          SceAvcdecArrayPicture *, SceInt32 *);
SceInt32 sceAvcdecDecodeStop(SceAvcdecCtrl *, SceAvcdecArrayPicture *);
SceUID sceCodecEngineOpenUnmapMemBlock(void *, SceSize);
SceInt32 sceCodecEngineCloseUnmapMemBlock(SceUID);
SceUIntVAddr sceCodecEngineAllocMemoryFromUnmapMemBlock(SceUID, SceUInt32, SceUInt32);
SceInt32 sceCodecEngineFreeMemoryFromUnmapMemBlock(SceUID, SceUIntVAddr);
}
#ifdef __vita__
static_assert(sizeof(SceVideodecCtrl) == 20 && sizeof(SceAvcdecPicture) == 100,
              "Unexpected Vita decoder ABI");
#endif
inline void codecCheck(const char *stage, int result) {
    if (result >= 0)
        return;
    char text[144];
    snprintf(text, sizeof(text), "HW video %s failed: 0x%08x (%d)", stage, unsigned(result), result);
    fprintf(stderr, "%s\n", text);
    throw std::runtime_error(text);
}
inline void logCodecMemory(const char *stage) {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof(info);
    int result = sceKernelGetFreeMemorySize(&info);
    if (result >= 0)
        fprintf(stderr, "HW free memory %s: main=%d CDRAM=%d PHYCONT=%d bytes\n", stage,
                info.size_user, info.size_cdram, info.size_phycont);
    else
        fprintf(stderr, "HW free memory query %s: 0x%08x\n", stage, unsigned(result));
}
class CodecMemory {
    SceUID uid = -1;

  public:
    void *data = nullptr;
    uint32_t size = 0;
    CodecMemory() = default;
    CodecMemory(const CodecMemory &) = delete;
    ~CodecMemory() {
        reset();
    }
    void reset() {
        if (uid >= 0)
            sceKernelFreeMemBlock(uid);
        uid = -1;
        data = nullptr;
        size = 0;
    }
    void allocate(const char *name, uint32_t bytes, bool physical) {
        reset();
        // SDL GXM reserves every remaining CDRAM page for its texture pool on the
        // first texture. AVC must use the separate physically contiguous main pool.
        // PHYCONT blocks have 1 MiB size/alignment granularity and use no opt struct,
        // matching the PHY variant of the reference h264_vita allocator.
        uint32_t unit = physical ? 0x100000u : 0x1000u;
        auto type = physical ? SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW
                             : SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE;
        size = (bytes + unit - 1) & ~(unit - 1);
        uid = sceKernelAllocMemBlock(name, type, size, nullptr);
        if (uid < 0) {
            fprintf(stderr, "HW allocation request: %s type=0x%08x bytes=%u rounded=%u\n", name,
                    unsigned(type), bytes, size);
            logCodecMemory("on allocation failure");
        }
        codecCheck(name, uid);
        codecCheck("GetMemBlockBase", sceKernelGetMemBlockBase(uid, &data));
        if (!data)
            throw std::runtime_error("HW video null memory block");
        fprintf(stderr, "HW memory: %s pool=%s size=%u uid=0x%x\n", name,
                physical ? "PHYCONT" : "main-uncached", size, unsigned(uid));
    }

};
class VitaHardwareCodec {
    static constexpr auto avc = SCE_VIDEODEC_TYPE_HW_AVCDEC;
    bool moduleOwned = false, videoLibrary = false, videoDecoder = false, audioLibrary = false,
         audioDecoder = false;
    CodecMemory context, frames, output, audioEs, audioPcm;
    SceUID unmap = -1;
    SceUIntVAddr address = 0;
    SceAvcdecCtrl video{};
    SceAudiodecCtrl audio{};
    SceAudiodecInfo audioInfo{};
    uint32_t pitch = 0, height = 0;

  public:
    ~VitaHardwareCodec() {
        close();
    }
    void close() {
        if (audioDecoder)
            sceAudiodecDeleteDecoder(&audio);
        if (audioLibrary)
            sceAudiodecTermLibrary(SCE_AUDIODEC_TYPE_AAC);
        audioDecoder = audioLibrary = false;
        audioEs.reset();
        audioPcm.reset();
        if (videoDecoder)
            sceAvcdecDeleteDecoder(&video);
        if (videoLibrary)
            sceVideodecTermLibrary(avc);
        videoDecoder = videoLibrary = false;
        if (address && unmap >= 0)
            sceCodecEngineFreeMemoryFromUnmapMemBlock(unmap, address);
        address = 0;
        if (unmap >= 0)
            sceCodecEngineCloseUnmapMemBlock(unmap);
        unmap = -1;
        output.reset();
        frames.reset();
        context.reset();
        if (moduleOwned)
            sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
        moduleOwned = false;
        video = {};
        audio = {};
        audioInfo = {};
    }
    void open(uint32_t w, uint32_t h, uint32_t refs, uint32_t rate, uint32_t channels) {
        close();
        try {
            // AVPLAYER loads the codec modules. No AvPlayer handle or firmware hooks are used.
            if (sceSysmoduleIsLoaded(SCE_SYSMODULE_AVPLAYER) != SCE_SYSMODULE_LOADED) {
                codecCheck("Load codec modules", sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER));
                moduleOwned = true;
            }
            pitch = (w + 15) & ~15u;
            height = (h + 15) & ~15u;
            SceVideodecQueryInitInfo query{};
            query.hwAvc = {sizeof(SceVideodecQueryInitInfoHwAvcdec), pitch, height, refs, 1};
            sceVideodecSetConfigInternal(avc, 2);
            sceAvcdecSetDecodeMode(avc, 0x80);
            uint32_t needed = 0;
            codecCheck("Query AVC context size", sceVideodecQueryMemSizeInternal(avc, &query, &needed));
            if (!needed || needed > 32 * 1024 * 1024)
                throw std::runtime_error("HW video QueryMemSize returned invalid size");
            SceAvcdecQueryDecoderInfo decoderQuery{pitch, height, refs};
            SceAvcdecDecoderInfo info{};
            codecCheck("QueryDecoderMemSize",
                       sceAvcdecQueryDecoderMemSizeInternal(avc, &decoderQuery, &info));
            if (!info.frameMemSize || info.frameMemSize > 32 * 1024 * 1024)
                throw std::runtime_error("HW video invalid reference buffer size");
            fprintf(stderr, "HW AVC config: visible=%ux%u coded=%ux%u refs=%u context=%u frames=%u\n", w, h,
                    pitch, height, refs, needed, info.frameMemSize);
            logCodecMemory("before AVC buffers");
            context.allocate("avc-context", needed, true);
            frames.allocate("avc-references", info.frameMemSize, true);
            output.allocate("avc-nv12", pitch * height * 3 / 2, true);
            logCodecMemory("after AVC buffers");
            unmap = sceCodecEngineOpenUnmapMemBlock(context.data, context.size);
            codecCheck("CodecEngineOpenUnmapMemBlock", unmap);
            if (!unmap)
                throw std::runtime_error("HW video invalid codec memory handle");
            address = sceCodecEngineAllocMemoryFromUnmapMemBlock(unmap, needed, 0x40000);
            if (!address)
                throw std::runtime_error("HW video CodecEngineAllocMemory returned null");
            SceVideodecCtrl library{};
            library.vaContext = address;
            library.contextSize = needed;
            codecCheck("InitLibraryWithUnmapMem",
                       sceVideodecInitLibraryWithUnmapMemInternal(avc, &library, &query));
            videoLibrary = true;
            video.frameBuf = {frames.data, info.frameMemSize};
            codecCheck("Create AVC decoder", sceAvcdecCreateDecoderInternal(avc, &video, &decoderQuery));
            videoDecoder = true;
            fprintf(stderr, "HW AVC decoder ready\n");
            SceAudiodecInitParam init{};
            init.aac = {sizeof(init.aac), 1};
            codecCheck("Init AAC library", sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AAC, &init));
            audioLibrary = true;
            audioEs.allocate("aac-input", SCE_AUDIODEC_AAC_MAX_ES_SIZE, false);
            audioPcm.allocate("aac-pcm", channels * SCE_AUDIODEC_AAC_MAX_SAMPLES * 2, false);
            audioInfo.aac = {sizeof(audioInfo.aac), 0, channels, rate, 1};
            audio.size = sizeof(audio);
            audio.pInfo = &audioInfo;
            audio.wordLength = 16;
            audio.pEs = static_cast<uint8_t *>(audioEs.data);
            audio.maxEsSize = SCE_AUDIODEC_AAC_MAX_ES_SIZE;
            audio.pPcm = static_cast<uint8_t *>(audioPcm.data);
            audio.maxPcmSize = channels * SCE_AUDIODEC_AAC_MAX_SAMPLES * 2;
            codecCheck("Create AAC decoder", sceAudiodecCreateDecoder(&audio, SCE_AUDIODEC_TYPE_AAC));
            audioDecoder = true;
            fprintf(stderr, "HW AAC decoder ready: rate=%u channels=%u\n", rate, channels);
        } catch (...) {
            close();
            throw;
        }
    }
    // Output storage is owned until the next video call; SDL copies it before reuse.
    bool decodeVideo(const std::vector<uint8_t> *packet, uint64_t pts, bool &accepted, bool &ended,
                     uint64_t &outputPts) {
        SceAvcdecPicture picture{};
        picture.size = sizeof(picture);
        picture.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER;
        picture.frame.framePitch = pitch;
        picture.frame.frameWidth = pitch;
        picture.frame.frameHeight = height;
        picture.frame.pPicture[0] = output.data;
        SceAvcdecPicture *ptr = &picture;
        SceAvcdecArrayPicture pictures{};
        pictures.numOfElm = 1;
        pictures.pPicture = &ptr;
        accepted = false;
        ended = false;
        if (packet) {
            int space = sceAvcdecDecodeAvailableSize(&video);
            codecCheck("AVC available ES bytes", space);
            SceAvcdecAu au{};
            au.pts = {uint32_t(pts >> 32), uint32_t(pts)};
            au.dts = au.pts;
            if (size_t(space) >= packet->size()) {
                au.es.pBuf = const_cast<uint8_t *>(packet->data());
                au.es.size = uint32_t(packet->size());
            }
            int pictureId = 0;
            int result = sceAvcdecDecodeAuInternal(&video, &au, &pictureId);
            if (uint32_t(result) != SCE_AVCDEC_ERROR_ES_BUFFER_FULL) {
                codecCheck("Decode AVC access unit", result);
                accepted = au.es.size != 0;
            }
            // This is the split decode/retrieve sequence used by MIT ReAvPlayer.
            SceAvcdecArrayPicture work{};
            codecCheck("Retrieve AVC picture", sceAvcdecDecodeGetPictureWithWorkPictureInternal(
                                                   &video, &pictures, &work, &pictureId));
        } else {
            codecCheck("Drain AVC decoder", sceAvcdecDecodeStop(&video, &pictures));
            ended = pictures.numOfOutput == 0;
        }
        if (pictures.numOfOutput > 1)
            throw std::runtime_error("HW video exceeded output capacity");
        if (!pictures.numOfOutput)
            return false;
        if (picture.frame.framePitch != pitch || picture.frame.frameHeight != height)
            throw std::runtime_error("HW video unexpected decoded NV12 layout");
        outputPts = (uint64_t(picture.info.pts.upper) << 32) | picture.info.pts.lower;
        return true;
    }
    const uint8_t *pixels() const {
        return static_cast<const uint8_t *>(output.data);
    }
    uint32_t stride() const {
        return pitch;
    }
    uint32_t rows() const {
        return height;
    }
    uint32_t decodeAudio(const std::vector<uint8_t> &packet) {
        if (packet.size() > audio.maxEsSize)
            throw std::runtime_error("AAC packet exceeds decoder capacity");
        memcpy(audio.pEs, packet.data(), packet.size());
        audio.inputEsSize = uint32_t(packet.size());
        audio.outputPcmSize = 0;
        codecCheck("Decode AAC access unit", sceAudiodecDecode(&audio));
        if (audio.outputPcmSize > audio.maxPcmSize || audio.outputPcmSize % 4)
            throw std::runtime_error("HW audio invalid PCM size");
        return audio.outputPcmSize / 4;
    }
    const uint8_t *pcm() const {
        return static_cast<const uint8_t *>(audioPcm.data);
    }
};
