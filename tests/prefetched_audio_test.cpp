#define SDL_MAIN_HANDLED
#include "prefetched_audio.hpp"
#include "archive_audio.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
using namespace tenshi;

static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static void waitFor(const std::function<bool()> &predicate) {
    uint32_t start = SDL_GetTicks();
    while (!predicate()) {
        if (SDL_GetTicks() - start > 4000) throw std::runtime_error("Prefetch wait timed out");
        SDL_Delay(1);
    }
}
struct Synthetic {
    std::filesystem::path path;
    Bytes expected;
    Synthetic(int bits, size_t length = 64 * 6 + 12) {
        Bytes entry(18 + length);
        int channels = bits == 16 ? 2 : 1, align = channels * bits / 8;
        entry[0] = uint8_t(channels); entry[1] = uint8_t(align);
        entry[2] = uint8_t(44100); entry[3] = uint8_t(44100 >> 8);
        entry[4] = uint8_t(bits);
        put32(entry, 6, 44100 * align); put32(entry, 10, uint32_t(length));
        for (size_t i = 0; i < length; ++i) entry[18 + i] = uint8_t(1 + i % 127);
        expected = wave(entry);
        Bytes archive(36 + entry.size());
        archive[0] = 0x1e; archive[1] = 0xaf; archive[2] = 1;
        std::memcpy(archive.data() + 4, "test.w", 6);
        put32(archive, 28, uint32_t(entry.size()));
        std::copy(entry.begin(), entry.end(), archive.begin() + 36);
        path = std::filesystem::temp_directory_path() /
               ("tenshi-prefetch-" + std::to_string(SDL_GetTicks()) + "-" + std::to_string(bits) + ".a");
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
        check(bool(file), "Cannot write synthetic audio archive");
    }
    ~Synthetic() { std::error_code error; std::filesystem::remove(path, error); }
};
using Stream = std::unique_ptr<SDL_RWops, int (*)(SDL_RWops *)>;
struct ReleaseGate {
    std::atomic<bool> &released;
    ~ReleaseGate() { released = true; }
};

static void byteChecks(int bits) {
    Synthetic fixture(bits);
    Archive archive(fixture.path.string());
    PrefetchedArchiveWave::Options options; options.pageBytes = 64;
    Stream rw(PrefetchedArchiveWave::open(archive, "test.w", options), SDL_RWclose);
    auto &s = PrefetchedArchiveWave::get(rw.get());
    check(SDL_RWsize(rw.get()) == Sint64(fixture.expected.size()), "Stream length mismatch");
    // Reproduce SDL_mixer's metadata scan: seek EOF then return to PCM start.
    check(SDL_RWseek(rw.get(), 0, RW_SEEK_END) == Sint64(fixture.expected.size()), "EOF seek failed");
    SDL_Delay(10);
    check(s.cached(44, 128), "EOF metadata scan evicted initial prefill");
    SDL_RWseek(rw.get(), 0, RW_SEEK_SET);
    Bytes actual(fixture.expected.size());
    size_t at = 0;
    while (at < actual.size()) {
        size_t n = std::min(size_t(43), actual.size() - at);
        waitFor([&] { return s.cached(Sint64(at), n); });
        check(SDL_RWread(rw.get(), actual.data() + at, 1, n) == n, "Sequential read short");
        at += n;
    }
    check(actual == fixture.expected, "Complete PCM bytes differ");
    check(SDL_RWread(rw.get(), actual.data(), 1, 1) == 0, "EOF returned data");
    check(SDL_RWseek(rw.get(), 1, RW_SEEK_END) == -1 &&
          SDL_RWseek(rw.get(), -1, RW_SEEK_SET) == -1 &&
          SDL_RWseek(rw.get(), 0, 900) == -1, "Seek bounds not enforced");
    check(SDL_RWseek(rw.get(), 0, RW_SEEK_CUR) == Sint64(actual.size()), "Invalid seek changed position");
    // The end page prefetches page zero before SDL_mixer seeks back to loop.
    check(s.cached(44, 64), "Loop start page not prefetched");
    check(SDL_RWseek(rw.get(), 44, RW_SEEK_SET) == 44, "Loop seek failed");
    Bytes beginning(64);
    check(SDL_RWread(rw.get(), beginning.data(), 1, beginning.size()) == beginning.size() &&
          std::equal(beginning.begin(), beginning.end(), fixture.expected.begin() + 44), "Loop start changed");
    for (size_t atSeek : {size_t(17), size_t(40), size_t(43), size_t(44), size_t(165), actual.size() - 3})
        for (size_t unit : {size_t(1), size_t(2), size_t(4)}) {
            SDL_RWseek(rw.get(), Sint64(atSeek), RW_SEEK_SET);
            size_t n = std::min(size_t(7), (actual.size() - atSeek) / unit);
            waitFor([&] { return s.cached(Sint64(atSeek), n * unit); });
            Bytes got(7 * unit);
            check(SDL_RWread(rw.get(), got.data(), unit, 7) == n &&
                  std::equal(got.begin(), got.begin() + n * unit, fixture.expected.begin() + atSeek),
                  "Partial object or seek bytes differ");
        }
    check(SDL_RWseek(rw.get(), -1, RW_SEEK_END) == Sint64(actual.size() - 1), "Tail seek failed");
    check(SDL_RWread(rw.get(), actual.data(), 4, 1) == 0 &&
          SDL_RWseek(rw.get(), 0, RW_SEEK_CUR) == Sint64(actual.size() - 1), "Incomplete object advanced position");
    auto stats = s.stats();
    check(!stats.underrunBytes && !stats.ioErrors && stats.bufferBytes <= 4 * options.pageBytes,
          "Unexpected underrun, I/O error or excessive buffers");
}

static uint32_t slowReadChecks(int bits) {
    Synthetic fixture(bits);
    Archive archive(fixture.path.string());
    std::atomic<bool> blocked{false}, entered{false}, release{false};
    std::atomic<unsigned> diskReads{0};
    PrefetchedArchiveWave::Options options; options.pageBytes = 64;
    options.beforeRead = [&] {
        ++diskReads;
        if (blocked) {
            entered = true;
            while (!release) SDL_Delay(1);
        }
    };
    Stream rw(PrefetchedArchiveWave::open(archive, "test.w", options), SDL_RWclose);
    ReleaseGate releaseOnFailure{release};
    auto &s = PrefetchedArchiveWave::get(rw.get());
    blocked = true;
    SDL_RWseek(rw.get(), 44 + 64, RW_SEEK_SET); // current page 1; worker fetches page 2.
    waitFor([&] { return bool(entered); });
    Bytes got(64);
    uint32_t start = SDL_GetTicks();
    size_t n = SDL_RWread(rw.get(), got.data(), 1, 20);
    uint32_t elapsed = SDL_GetTicks() - start;
    check(n == 20 && std::equal(got.begin(), got.begin() + 20, fixture.expected.begin() + 44 + 64),
          "Cached read failed while disk worker stalled");
    check(elapsed < 40, "Cached read waited for slow disk");
    SDL_RWseek(rw.get(), 44 + 64 * 3, RW_SEEK_SET);
    start = SDL_GetTicks();
    n = SDL_RWread(rw.get(), got.data(), 1, got.size());
    elapsed = std::max(elapsed, SDL_GetTicks() - start);
    check(n == got.size() && elapsed < 40, "Cache miss blocked or reported false EOF");
    check(std::all_of(got.begin(), got.end(), [&](uint8_t b) { return b == (bits == 8 ? 128 : 0); }),
          "Underrun silence has wrong sample format");
    check(SDL_RWseek(rw.get(), 0, RW_SEEK_CUR) == 44 + 64 * 4 && s.stats().underrunBytes == 64,
          "Underrun did not advance/count its bytes");
    release = true;
    waitFor([&] { return s.cached(44 + 64 * 4, 64); });
    check(SDL_RWread(rw.get(), got.data(), 1, 64) == 64 &&
          std::equal(got.begin(), got.end(), fixture.expected.begin() + 44 + 64 * 4),
          "Playback did not recover after slow disk");
    rw.reset();
    unsigned readsAtClose = diskReads;
    SDL_Delay(20);
    check(diskReads == readsAtClose, "Worker kept reading after stream closed");
    return elapsed;
}

static void shortTailLoop() {
    const size_t page = PrefetchedArchiveWave::PageBytes, tail = 25632;
    Synthetic fixture(16, page * 2 + tail);
    Archive archive(fixture.path.string());
    std::atomic<bool> slow{false};
    PrefetchedArchiveWave::Options options;
    options.beforeRead = [&] { if (slow) SDL_Delay(220); };
    Stream rw(PrefetchedArchiveWave::open(archive, "test.w", options), SDL_RWclose);
    auto &s = PrefetchedArchiveWave::get(rw.get());
    slow = true;
    SDL_RWseek(rw.get(), Sint64(44 + 2 * page), RW_SEEK_SET);
    waitFor([&] { return s.cached(Sint64(44 + 2 * page), tail); });
    Bytes got(tail);
    check(SDL_RWread(rw.get(), got.data(), 1, tail) == tail &&
          std::equal(got.begin(), got.end(), fixture.expected.end() - tail), "Short tail PCM differs");
    // This tail has only 145 ms at the original rate, less than the imposed
    // 220 ms page I/O. The permanently resident first page must survive it.
    check(s.cached(44, page), "Short tail lost permanent loop head");
    SDL_RWseek(rw.get(), 44, RW_SEEK_SET);
    uint32_t start = SDL_GetTicks();
    check(SDL_RWread(rw.get(), got.data(), 1, got.size()) == got.size() &&
          std::equal(got.begin(), got.end(), fixture.expected.begin() + 44), "Slow short-tail loop differed");
    check(SDL_GetTicks() - start < 40 && s.stats().underrunBytes == 0,
          "Short-tail loop waited for disk or produced silence");
}

static void closeBusyWorker() {
    Synthetic fixture(16);
    Archive archive(fixture.path.string());
    std::atomic<bool> blocked{false}, entered{false}, release{false}, closed{false};
    PrefetchedArchiveWave::Options options; options.pageBytes = 64;
    options.beforeRead = [&] {
        if (blocked) { entered = true; while (!release) SDL_Delay(1); }
    };
    SDL_RWops *rw = PrefetchedArchiveWave::open(archive, "test.w", options);
    blocked = true;
    SDL_RWseek(rw, 44 + 64, RW_SEEK_SET);
    waitFor([&] { return bool(entered); });
    struct CloseJob { SDL_RWops *rw; std::atomic<bool> *closed; } job{rw, &closed};
    SDL_Thread *closer = SDL_CreateThread([](void *user) {
        auto &job = *static_cast<CloseJob *>(user);
        SDL_RWclose(job.rw); *job.closed = true;
        return 0;
    }, "prefetch-close-test", &job);
    check(closer != nullptr, "Cannot start close test");
    SDL_Delay(20);
    check(!closed, "Stream freed while disk reader still held it");
    release = true;
    SDL_WaitThread(closer, nullptr);
    check(closed, "Busy worker did not join on close");
}

static size_t originalChecks(const std::string &root) {
    Archive archive(root + "/music.a");
    size_t checked = 0;
    for (const auto &entry : archive.entries) {
        if (entry.first.size() < 2 || entry.first.substr(entry.first.size() - 2) != ".w") continue;
        Stream stream(PrefetchedArchiveWave::open(archive, entry.first), SDL_RWclose);
        Stream expected(ArchiveWave::open(archive, entry.first), SDL_RWclose);
        auto &s = PrefetchedArchiveWave::get(stream.get());
        Bytes got(32768), reference(got.size());
        Sint64 length = SDL_RWsize(expected.get());
        for (Sint64 at = 0; at < length;) {
            size_t n = size_t(std::min(Sint64(got.size()), length - at));
            waitFor([&] { return s.cached(at, n); });
            check(SDL_RWread(stream.get(), got.data(), 1, n) == n &&
                  SDL_RWread(expected.get(), reference.data(), 1, n) == n &&
                  std::equal(got.begin(), got.begin() + n, reference.begin()), "Original music PCM differs");
            at += Sint64(n);
        }
        auto stats = s.stats();
        check(stats.bufferBytes <= 4 * PrefetchedArchiveWave::PageBytes && !stats.underrunBytes && !stats.ioErrors,
              "Original music prefetch exceeded its budget or missed data");
        ++checked;
    }
    return checked;
}
int main(int argc, char **argv) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) < 0) return 1;
    try {
        byteChecks(16); byteChecks(8);
        uint32_t maxReadMs = std::max(slowReadChecks(16), slowReadChecks(8));
        shortTailLoop();
        closeBusyWorker();
        size_t originals = argc > 1 ? originalChecks(argv[1]) : 0;
        std::cout << "Prefetch: PCM/header/seek/cross-page/loop/EOF passed; stalled disk max read "
                  << maxReadMs << " ms; silence recovery and worker cleanup passed; "
                  << originals << " full original tracks matched; PCM buffer budget <= 4 MiB\n";
        SDL_Quit(); return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        SDL_Quit(); return 1;
    }
}
