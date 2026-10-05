#pragma once

#include "Allocator.hpp"

namespace Aquamarine {
    class CDMAHeapAllocator;
    class CBackend;
    class CSwapchain;

    // A linear DMA-BUF allocated from Android's system DMA heap. This is the
    // Android equivalent of the buffer object a DRM/GBM allocator would own.
    class CDMAHeapBuffer : public IBuffer {
      public:
        virtual ~CDMAHeapBuffer();

        virtual eBufferCapability caps();
        virtual eBufferType       type();
        virtual void              update(const Hyprutils::Math::CRegion& damage);
        virtual bool              isSynchronous();
        virtual bool              good();
        virtual SDMABUFAttrs      dmabuf();

      private:
        CDMAHeapBuffer(const SAllocatorBufferParams& params, Hyprutils::Memory::CWeakPointer<CDMAHeapAllocator> allocator_);

        Hyprutils::Memory::CWeakPointer<CDMAHeapAllocator> allocator;
        SDMABUFAttrs                                      attrs{.success = false};

        friend class CDMAHeapAllocator;
    };

    class CDMAHeapAllocator : public IAllocator {
      public:
        ~CDMAHeapAllocator();
        static Hyprutils::Memory::CSharedPointer<CDMAHeapAllocator> create(Hyprutils::Memory::CWeakPointer<CBackend> backend_,
                                                                            const std::string& heapPath = "/dev/dma_heap/system");

        virtual Hyprutils::Memory::CSharedPointer<IBuffer> acquire(const SAllocatorBufferParams& params,
                                                                   Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain_);
        virtual Hyprutils::Memory::CSharedPointer<CBackend> getBackend();
        virtual int                                         drmFD();
        virtual eAllocatorType                              type();

        Hyprutils::Memory::CWeakPointer<CDMAHeapAllocator> self;

      private:
        CDMAHeapAllocator(int heapFD_, Hyprutils::Memory::CWeakPointer<CBackend> backend_, std::string heapPath_);

        int                                       heapFD = -1;
        std::string                               heapPath;
        Hyprutils::Memory::CWeakPointer<CBackend> backend;

        friend class CDMAHeapBuffer;
    };
}
