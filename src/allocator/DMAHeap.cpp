#include <aquamarine/allocator/DMAHeap.hpp>
#include <aquamarine/backend/Backend.hpp>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
#define SP CSharedPointer

Aquamarine::CDMAHeapBuffer::CDMAHeapBuffer(const SAllocatorBufferParams& params, CWeakPointer<CDMAHeapAllocator> allocator_) : allocator(allocator_) {
    if (!allocator || params.size.x <= 0 || params.size.y <= 0)
        return;

    const uint32_t format = params.format == DRM_FORMAT_INVALID ? DRM_FORMAT_XRGB8888 : params.format;
    if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("DMAHeap: unsupported format 0x{:x}", format));
        return;
    }

    const uint64_t width  = static_cast<uint64_t>(params.size.x);
    const uint64_t height = static_cast<uint64_t>(params.size.y);
    // Qualcomm importers accept linear RGB with a naturally aligned row. A
    // 64-byte row alignment also avoids partial cache lines on wide displays.
    const uint64_t stride = (width * 4U + 63U) & ~63U;
    const uint64_t bytes  = stride * height;
    const long     page   = sysconf(_SC_PAGESIZE);
    const uint64_t align  = page > 0 ? static_cast<uint64_t>(page) : 4096U;
    const uint64_t length = (bytes + align - 1U) & ~(align - 1U);

    if (stride > UINT32_MAX || length < bytes) {
        allocator->backend->log(AQ_LOG_ERROR, "DMAHeap: buffer dimensions overflow");
        return;
    }

    dma_heap_allocation_data allocation{
        .len        = length,
        .fd         = 0,
        .fd_flags   = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    if (ioctl(allocator->heapFD, DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("DMAHeap: allocation of {} bytes failed: {}", length, strerror(errno)));
        return;
    }

    attrs.success        = true;
    attrs.size           = params.size;
    attrs.format         = format;
    attrs.modifier       = DRM_FORMAT_MOD_LINEAR;
    attrs.planes         = 1;
    attrs.fds.at(0)      = static_cast<int>(allocation.fd);
    attrs.offsets.at(0)  = 0;
    attrs.strides.at(0)  = static_cast<uint32_t>(stride);
    size                 = params.size;
    opaque               = format == DRM_FORMAT_XRGB8888;

    allocator->backend->log(AQ_LOG_DEBUG,
                            std::format("DMAHeap: allocated {}x{} linear buffer, stride {}, fd {}", width, height, stride, attrs.fds.at(0)));
}

Aquamarine::CDMAHeapBuffer::~CDMAHeapBuffer() {
    events.destroy.emit();
    if (attrs.fds.at(0) >= 0)
        close(attrs.fds.at(0));
}

eBufferCapability Aquamarine::CDMAHeapBuffer::caps() {
    return BUFFER_CAPABILITY_NONE;
}

eBufferType Aquamarine::CDMAHeapBuffer::type() {
    return BUFFER_TYPE_DMABUF;
}

void Aquamarine::CDMAHeapBuffer::update(const Hyprutils::Math::CRegion& damage) {}

bool Aquamarine::CDMAHeapBuffer::isSynchronous() {
    return false;
}

bool Aquamarine::CDMAHeapBuffer::good() {
    return attrs.success && attrs.fds.at(0) >= 0;
}

SDMABUFAttrs Aquamarine::CDMAHeapBuffer::dmabuf() {
    return attrs;
}

Aquamarine::CDMAHeapAllocator::CDMAHeapAllocator(int heapFD_, CWeakPointer<CBackend> backend_, std::string heapPath_) :
    heapFD(heapFD_), heapPath(std::move(heapPath_)), backend(backend_) {}

Aquamarine::CDMAHeapAllocator::~CDMAHeapAllocator() {
    if (heapFD >= 0)
        close(heapFD);
}

SP<CDMAHeapAllocator> Aquamarine::CDMAHeapAllocator::create(CWeakPointer<CBackend> backend_, const std::string& heapPath) {
    const int fd = open(heapPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        backend_->log(AQ_LOG_ERROR, std::format("DMAHeap: cannot open {}: {}", heapPath, strerror(errno)));
        return nullptr;
    }

    auto allocator  = SP<CDMAHeapAllocator>(new CDMAHeapAllocator(fd, backend_, heapPath));
    allocator->self = allocator;
    backend_->log(AQ_LOG_DEBUG, std::format("DMAHeap: using Android system heap {}", heapPath));
    return allocator;
}

SP<IBuffer> Aquamarine::CDMAHeapAllocator::acquire(const SAllocatorBufferParams& params, SP<CSwapchain> swapchain_) {
    auto buffer = SP<IBuffer>(new CDMAHeapBuffer(params, self));
    return buffer->good() ? buffer : nullptr;
}

SP<CBackend> Aquamarine::CDMAHeapAllocator::getBackend() {
    return backend.lock();
}

int Aquamarine::CDMAHeapAllocator::drmFD() {
    return -1;
}

eAllocatorType Aquamarine::CDMAHeapAllocator::type() {
    return AQ_ALLOCATOR_TYPE_DMA_HEAP;
}
