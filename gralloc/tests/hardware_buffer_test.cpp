#include <android/hardware_buffer.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr uint64_t kCpuUsage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                               AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
using Buffer = std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)>;

void check(int result, const char* operation) {
    if (result != 0) {
        std::string status = std::to_string(result);
        std::string message = std::string(operation) + ": " + status;
        throw std::runtime_error(message);
    }
}

class LockedBuffer {
public:
    explicit LockedBuffer(AHardwareBuffer* buffer, uint64_t usage) : buffer_(buffer) {
        int result = AHardwareBuffer_lock(buffer_, usage, -1, nullptr, &data_);
        check(result, "lock");
    }

    ~LockedBuffer() {
        if (data_ != nullptr) {
            AHardwareBuffer_unlock(buffer_, nullptr);
        }
    }

    uint8_t* data() const {
        return static_cast<uint8_t*>(data_);
    }

    void unlock() {
        int result = AHardwareBuffer_unlock(buffer_, nullptr);
        data_ = nullptr;
        check(result, "unlock");
    }

private:
    AHardwareBuffer* buffer_;
    void* data_ = nullptr;
};

class SocketPair {
public:
    SocketPair() {
        int result = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, descriptors_);
        check(result, "socketpair");
    }

    ~SocketPair() {
        close(descriptors_[0]);
        close(descriptors_[1]);
    }

    int sender() const { return descriptors_[0]; }
    int receiver() const { return descriptors_[1]; }

private:
    int descriptors_[2];
};

void testBuffer(uint32_t format, uint32_t width, uint32_t height, size_t bytesPerPixel) {
    AHardwareBuffer_Desc requested = {};
    requested.width = width;
    requested.height = height;
    requested.layers = 1;
    requested.format = format;
    requested.usage = kCpuUsage;

    AHardwareBuffer* allocated = nullptr;
    int result = AHardwareBuffer_allocate(&requested, &allocated);
    check(result, "allocate");
    Buffer buffer(allocated, AHardwareBuffer_release);

    AHardwareBuffer_Desc actual = {};
    AHardwareBuffer_describe(allocated, &actual);
    if (actual.width != width || actual.height != height || actual.layers != 1 ||
        actual.format != format || actual.stride < width) {
        throw std::runtime_error("description differs from request");
    }
    if (format == AHARDWAREBUFFER_FORMAT_BLOB && actual.stride != width) {
        throw std::runtime_error("BLOB stride must equal its byte width");
    }

    size_t size = static_cast<size_t>(actual.stride) * height * bytesPerPixel;
    LockedBuffer writer(allocated, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN);
    uint8_t* writable = writer.data();
    for (size_t index = 0; index < size; ++index) {
        writable[index] = static_cast<uint8_t>(index * 37 + 11);
    }
    writer.unlock();

    SocketPair sockets;
    int sender = sockets.sender();
    result = AHardwareBuffer_sendHandleToUnixSocket(allocated, sender);
    check(result, "send handle");
    AHardwareBuffer* received = nullptr;
    int receiver = sockets.receiver();
    result = AHardwareBuffer_recvHandleFromUnixSocket(receiver, &received);
    check(result, "receive handle");
    Buffer imported(received, AHardwareBuffer_release);
    AHardwareBuffer_Desc importedDescription = {};
    AHardwareBuffer_describe(received, &importedDescription);
    if (importedDescription.width != actual.width || importedDescription.height != actual.height ||
        importedDescription.stride != actual.stride || importedDescription.format != format) {
        throw std::runtime_error("imported buffer description differs");
    }

    LockedBuffer reader(received, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN);
    uint8_t* readable = reader.data();
    for (size_t index = 0; index < size; ++index) {
        uint8_t expected = static_cast<uint8_t>(index * 37 + 11);
        if (readable[index] != expected) {
            throw std::runtime_error("imported buffer data differs");
        }
    }
    reader.unlock();
}

} // namespace

int main() {
    struct TestCase {
        uint32_t format;
        uint32_t width;
        uint32_t height;
        size_t bytesPerPixel;
    };
    const TestCase cases[] = {
        {AHARDWAREBUFFER_FORMAT_BLOB, 1, 1, 1},
        {AHARDWAREBUFFER_FORMAT_BLOB, 3, 1, 1},
        {AHARDWAREBUFFER_FORMAT_BLOB, 4095, 1, 1},
        {AHARDWAREBUFFER_FORMAT_BLOB, 4096, 1, 1},
        {AHARDWAREBUFFER_FORMAT_BLOB, 4097, 1, 1},
        {AHARDWAREBUFFER_FORMAT_BLOB, 12345, 1, 1},
        {AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM, 17, 9, 2},
        {AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, 17, 9, 4},
        {AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT, 17, 9, 8},
    };
    int failures = 0;
    for (const TestCase& test : cases) {
        try {
            testBuffer(test.format, test.width, test.height, test.bytesPerPixel);
            std::printf("PASS format=%u width=%u height=%u\n", test.format, test.width, test.height);
        } catch (const std::exception& error) {
            const char* message = error.what();
            std::printf("FAIL format=%u width=%u height=%u: %s\n", test.format, test.width, test.height, message);
            ++failures;
        }
    }
    std::printf("failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
