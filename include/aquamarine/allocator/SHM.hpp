#pragma once

#include "Allocator.hpp"

namespace Aquamarine {
    class CSHMAllocator;
    class CBackend;
    class CSwapchain;

    class CSHMBuffer : public IBuffer {
      public:
        virtual ~CSHMBuffer();

        virtual eBufferCapability                      caps();
        virtual eBufferType                            type();
        virtual void                                   update(const Hyprutils::Math::CRegion& damage);
        virtual bool                                   isSynchronous();
        virtual bool                                   good();
        virtual SSHMAttrs                              shm();
        virtual std::tuple<uint8_t*, uint32_t, size_t> beginDataPtr(uint32_t flags);
        virtual void                                   endDataPtr();

      private:
        CSHMBuffer(const SAllocatorBufferParams& params, Hyprutils::Memory::CWeakPointer<CSHMAllocator> allocator_);

        Hyprutils::Memory::CWeakPointer<CSHMAllocator> allocator;
        SSHMAttrs                                      attrs{.success = false};
        uint8_t*                                       data      = nullptr;
        size_t                                         bufferLen = 0;

        friend class CSHMAllocator;
    };

    class CSHMAllocator : public IAllocator {
      public:
        ~CSHMAllocator();
        static Hyprutils::Memory::CSharedPointer<CSHMAllocator> create(Hyprutils::Memory::CWeakPointer<CBackend> backend_);

        virtual Hyprutils::Memory::CSharedPointer<IBuffer> acquire(const SAllocatorBufferParams& params,
                                                                   Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain_);
        virtual Hyprutils::Memory::CSharedPointer<CBackend> getBackend();
        virtual int                                         drmFD();
        virtual eAllocatorType                              type();

        Hyprutils::Memory::CWeakPointer<CSHMAllocator> self;

      private:
        CSHMAllocator(Hyprutils::Memory::CWeakPointer<CBackend> backend_);
        Hyprutils::Memory::CWeakPointer<CBackend> backend;

        friend class CSHMBuffer;
    };
}
