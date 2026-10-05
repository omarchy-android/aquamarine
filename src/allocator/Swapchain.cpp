#include <algorithm>
#include <aquamarine/allocator/Swapchain.hpp>
#include <aquamarine/backend/Backend.hpp>
#include "FormatUtils.hpp"

using namespace Aquamarine;
using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;
#define SP CSharedPointer

SP<CSwapchain> Aquamarine::CSwapchain::create(SP<IAllocator> allocator_, SP<IBackendImplementation> backendImpl_) {
    auto p  = SP<CSwapchain>(new CSwapchain(allocator_, backendImpl_));
    p->self = p;
    return p;
}

Aquamarine::CSwapchain::CSwapchain(SP<IAllocator> allocator_, SP<IBackendImplementation> backendImpl_) : allocator(allocator_), backendImpl(backendImpl_) {
    if (!allocator || !backendImpl)
        return;
}

bool Aquamarine::CSwapchain::reconfigure(const SSwapchainOptions& options_) {
    if (!allocator)
        return false;

    if (options_.size == Vector2D{} || options_.length == 0) {
        // clear the swapchain
        allocator->getBackend()->log(AQ_LOG_DEBUG, "Swapchain: Clearing");
        buffers.clear();
        options = options_;
        return true;
    }

    if ((options_.format == options.format || options_.format == DRM_FORMAT_INVALID) && options_.size == options.size && options_.length == options.length &&
        buffers.size() == options.length)
        return true; // no need to reconfigure

    if ((options_.format == options.format || options_.format == DRM_FORMAT_INVALID) && options_.size == options.size) {
        bool ok = resize(options_.length);
        if (!ok)
            return false;

        options = options_;

        allocator->getBackend()->log(AQ_LOG_DEBUG, std::format("Swapchain: Resized a {} {} swapchain to length {}", options.size, fourccToName(options.format), options.length));
        return true;
    }

    bool ok = fullReconfigure(options_);
    if (!ok)
        return false;

    options = options_;
    if (options.format == DRM_FORMAT_INVALID) {
        const auto dma = buffers.at(0)->dmabuf();
        const auto shm = buffers.at(0)->shm();
        options.format = dma.success ? dma.format : shm.format;
    }

    allocator->getBackend()->log(AQ_LOG_DEBUG,
                                 std::format("Swapchain: Reconfigured a swapchain to {} {} of length {}", options.size, fourccToName(options.format), options.length));
    return true;
}

SP<IBuffer> Aquamarine::CSwapchain::next(int* age) {
    if (!allocator || options.length <= 0)
        return nullptr;

    // A nested Wayland compositor owns a submitted wl_buffer until it sends
    // wl_buffer.release. Reusing such a buffer for a new render pass races the
    // parent compositor and presents stale or partially-updated pixels. This
    // matters especially for SHM, where the renderer writes directly into the
    // memory Weston is still reading.
    //
    // Backends mark their in-flight buffers with IBuffer::lock(). Walk the
    // whole ring and acquire only a buffer that the backend has released.
    // Returning nullptr when the ring is exhausted lets the pending frame
    // callback wake the renderer once the parent has consumed a buffer.
    for (size_t i = 0; i < options.length; ++i) {
        const auto candidate = (lastAcquired + 1 + i) % options.length;
        if (buffers.at(candidate)->locked())
            continue;

        lastAcquired = candidate;

        if (age)
            *age = options.length; // we always just rotate

        return buffers.at(lastAcquired);
    }

    allocator->getBackend()->log(AQ_LOG_TRACE, "Swapchain: all buffers are still owned by the backend");
    return nullptr;
}

bool Aquamarine::CSwapchain::fullReconfigure(const SSwapchainOptions& options_) {
    std::vector<Hyprutils::Memory::CSharedPointer<IBuffer>> bfs;
    bfs.reserve(options_.length);

    for (size_t i = 0; i < options_.length; ++i) {
        auto buf = allocator->acquire(
            SAllocatorBufferParams{.size = options_.size, .format = options_.format, .scanout = options_.scanout, .cursor = options_.cursor, .multigpu = options_.multigpu},
            self.lock());
        if (!buf) {
            allocator->getBackend()->log(AQ_LOG_ERROR, "Swapchain: Failed acquiring a buffer");
            return false;
        }
        const auto dma = buf->dmabuf();
        const auto shm = buf->shm();
        if (dma.success)
            allocator->getBackend()->log(AQ_LOG_TRACE,
                                         std::format("Swapchain: Acquired a buffer with format {} and modifier 0x{:x} : {}", fourccToName(dma.format), dma.modifier,
                                                     drmModifierToName(dma.modifier)));
        else
            allocator->getBackend()->log(AQ_LOG_TRACE, std::format("Swapchain: Acquired a SHM buffer with format {}", fourccToName(shm.format)));
        bfs.emplace_back(buf);
    }

    buffers = std::move(bfs);

    return true;
}

bool Aquamarine::CSwapchain::resize(size_t newSize) {
    if (newSize == buffers.size())
        return true;

    if (newSize < buffers.size()) {
        while (buffers.size() > newSize) {
            buffers.pop_back();
        }
    } else {
        while (buffers.size() < newSize) {
            auto buf = allocator->acquire(
                SAllocatorBufferParams{.size = options.size, .format = options.format, .scanout = options.scanout, .cursor = options.cursor, .multigpu = options.multigpu},
                self.lock());
            if (!buf) {
                allocator->getBackend()->log(AQ_LOG_ERROR, "Swapchain: Failed acquiring a buffer");
                return false;
            }
            buffers.emplace_back(buf);
        }
    }

    return true;
}

bool Aquamarine::CSwapchain::contains(SP<IBuffer> buffer) {
    return std::ranges::find(buffers, buffer) != buffers.end();
}

const SSwapchainOptions& Aquamarine::CSwapchain::currentOptions() {
    return options;
}

void Aquamarine::CSwapchain::rollback() {
    lastAcquired--;
    if (lastAcquired < 0)
        lastAcquired = options.length - 1;
}

SP<IAllocator> Aquamarine::CSwapchain::getAllocator() {
    return allocator;
}
