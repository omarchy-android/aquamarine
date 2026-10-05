#include <aquamarine/buffer/Buffer.hpp>
#include "Shared.hpp"

#include <fcntl.h>
#include <unistd.h>

using namespace Aquamarine;

SDMABUFAttrs Aquamarine::IBuffer::dmabuf() {
    return SDMABUFAttrs{};
}

SSHMAttrs Aquamarine::IBuffer::shm() {
    return SSHMAttrs{};
}

std::tuple<uint8_t*, uint32_t, size_t> Aquamarine::IBuffer::beginDataPtr(uint32_t flags) {
    return {nullptr, 0, 0};
}

void Aquamarine::IBuffer::endDataPtr() {
    ; // empty
}

void Aquamarine::IBuffer::sendRelease() {
    ;
}

void Aquamarine::IBuffer::lock() {
    locks++;
}

void Aquamarine::IBuffer::unlock() {
    locks--;

    ASSERT(locks >= 0);

    if (locks <= 0)
        sendRelease();
}

bool Aquamarine::IBuffer::locked() {
    return locks;
}

void Aquamarine::IBuffer::backendPin() {
    backendPins++;
    lockedByBackend = true;
}

void Aquamarine::IBuffer::backendUnpin() {
    ASSERT(backendPins > 0);

    if (backendPins == 0)
        return;

    backendPins--;
    if (backendPins > 0)
        return;

    lockedByBackend = false;
    events.backendRelease.emit();
}

uint32_t Aquamarine::IBuffer::backendPinCount() const {
    return backendPins;
}

bool Aquamarine::IBuffer::setPresentationDMABUF(const SDMABUFAttrs& attrs) {
    clearPresentationDMABUF();

    if (!attrs.success || attrs.planes < 1 || attrs.planes > 4 || attrs.size.x <= 0 || attrs.size.y <= 0)
        return false;

    auto duplicated    = attrs;
    duplicated.success = false;
    duplicated.fds.fill(-1);

    for (int i = 0; i < attrs.planes; ++i) {
        if (attrs.fds.at(i) < 0) {
            for (int j = 0; j < i; ++j)
                close(duplicated.fds.at(j));
            return false;
        }

        duplicated.fds.at(i) = fcntl(attrs.fds.at(i), F_DUPFD_CLOEXEC, 0);
        if (duplicated.fds.at(i) < 0) {
            for (int j = 0; j < i; ++j)
                close(duplicated.fds.at(j));
            return false;
        }
    }

    duplicated.success        = true;
    presentationDMABUFAttrs   = duplicated;
    return true;
}

SDMABUFAttrs Aquamarine::IBuffer::presentationDMABUF() const {
    return presentationDMABUFAttrs;
}

void Aquamarine::IBuffer::clearPresentationDMABUF() {
    for (int i = 0; i < presentationDMABUFAttrs.planes && i < 4; ++i) {
        if (presentationDMABUFAttrs.fds.at(i) >= 0)
            close(presentationDMABUFAttrs.fds.at(i));
    }
    presentationDMABUFAttrs    = {};
    presentationDMABUFIsActive = false;
}

void Aquamarine::IBuffer::setPresentationDMABUFActive(bool active) {
    presentationDMABUFIsActive = active;
}

bool Aquamarine::IBuffer::presentationDMABUFActive() const {
    return presentationDMABUFIsActive;
}
