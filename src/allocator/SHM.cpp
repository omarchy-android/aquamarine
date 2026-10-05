#include <aquamarine/allocator/SHM.hpp>
#include <aquamarine/backend/Backend.hpp>

#include <drm_fourcc.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
#define SP CSharedPointer

Aquamarine::CSHMBuffer::CSHMBuffer(const SAllocatorBufferParams& params, CWeakPointer<CSHMAllocator> allocator_) : allocator(allocator_) {
    if (params.size.x <= 0 || params.size.y <= 0)
        return;

    const uint32_t format = params.format == DRM_FORMAT_INVALID ? DRM_FORMAT_XRGB8888 : params.format;
    if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("SHM: unsupported format {}", format));
        return;
    }

    const int width  = static_cast<int>(params.size.x);
    const int height = static_cast<int>(params.size.y);
    const int stride = width * 4;
    bufferLen        = static_cast<size_t>(stride) * static_cast<size_t>(height);

    const int fd = static_cast<int>(syscall(SYS_memfd_create, "aquamarine-shm", MFD_CLOEXEC));
    if (fd < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("SHM: memfd_create failed: {}", strerror(errno)));
        return;
    }

    if (ftruncate(fd, static_cast<off_t>(bufferLen)) < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("SHM: ftruncate failed: {}", strerror(errno)));
        close(fd);
        return;
    }

    data = static_cast<uint8_t*>(mmap(nullptr, bufferLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (data == MAP_FAILED) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("SHM: mmap failed: {}", strerror(errno)));
        data = nullptr;
        close(fd);
        return;
    }

    memset(data, 0, bufferLen);

    size          = params.size;
    attrs.success = true;
    attrs.fd      = fd;
    attrs.format  = format;
    attrs.size    = params.size;
    attrs.stride  = stride;
    attrs.offset  = 0;
}

Aquamarine::CSHMBuffer::~CSHMBuffer() {
    events.destroy.emit();
    if (data)
        munmap(data, bufferLen);
    if (attrs.fd >= 0)
        close(attrs.fd);
}

eBufferCapability Aquamarine::CSHMBuffer::caps() {
    return BUFFER_CAPABILITY_DATAPTR;
}

eBufferType Aquamarine::CSHMBuffer::type() {
    return BUFFER_TYPE_SHM;
}

void Aquamarine::CSHMBuffer::update(const Hyprutils::Math::CRegion& damage) {}

bool Aquamarine::CSHMBuffer::isSynchronous() {
    return true;
}

bool Aquamarine::CSHMBuffer::good() {
    return attrs.success && data;
}

SSHMAttrs Aquamarine::CSHMBuffer::shm() {
    return attrs;
}

std::tuple<uint8_t*, uint32_t, size_t> Aquamarine::CSHMBuffer::beginDataPtr(uint32_t flags) {
    return {data, attrs.format, bufferLen};
}

void Aquamarine::CSHMBuffer::endDataPtr() {}

Aquamarine::CSHMAllocator::CSHMAllocator(CWeakPointer<CBackend> backend_) : backend(backend_) {}
Aquamarine::CSHMAllocator::~CSHMAllocator() = default;

SP<CSHMAllocator> Aquamarine::CSHMAllocator::create(CWeakPointer<CBackend> backend_) {
    auto allocator  = SP<CSHMAllocator>(new CSHMAllocator(backend_));
    allocator->self = allocator;
    backend_->log(AQ_LOG_DEBUG, "SHM: created a memory allocator for the nested Wayland backend");
    return allocator;
}

SP<IBuffer> Aquamarine::CSHMAllocator::acquire(const SAllocatorBufferParams& params, SP<CSwapchain> swapchain_) {
    auto buffer = SP<IBuffer>(new CSHMBuffer(params, self));
    return buffer->good() ? buffer : nullptr;
}

SP<CBackend> Aquamarine::CSHMAllocator::getBackend() {
    return backend.lock();
}

int Aquamarine::CSHMAllocator::drmFD() {
    return -1;
}

eAllocatorType Aquamarine::CSHMAllocator::type() {
    return AQ_ALLOCATOR_TYPE_SHM;
}
