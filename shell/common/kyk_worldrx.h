/* kyk_worldrx.h — reassembling a user world arriving over HostLink.
 *
 * Shared by both shells so the rules live in one place. They are:
 *
 *   In order, from zero. Random-access offsets would mean indexing a buffer
 *   with numbers a stranger chose, and the host gains nothing from them — it
 *   is sending a file it already holds, start to finish. Anything out of
 *   sequence resets the transfer rather than being patched in.
 *
 *   Nothing is loaded until the last byte arrives. A world that stops half way
 *   through leaves the running one untouched, so a cable pulled mid-transfer
 *   costs you the transfer and not the sound.
 *
 *   The size is checked against the buffer before a byte is copied, not after.
 *
 * Status codes match the extension's convention: 0 ok, 1 refused, 2 malformed.
 */
#pragma once
#include "kyk_userworld.h"

namespace kyk {

class WorldReceiver
{
public:
    /* 0 = accepted and still receiving, 0 = accepted and loaded (see Done),
     * 1 = refused, 2 = malformed. */
    uint8_t Take(uint32_t total, uint32_t off, const uint8_t* data, int len)
    {
        done_ = false;
        if(len < 0 || total == 0 || total > kUserBlobMax) return 2u;
        if(off == 0) { have_ = 0; total_ = total; }
        else if(total != total_ || off != have_) { have_ = 0; total_ = 0; return 2u; }
        if((size_t)off + (size_t)len > total_) { have_ = 0; total_ = 0; return 2u; }
        if(len) std::memcpy(buf_ + off, data, (size_t)len);
        have_ = off + (uint32_t)len;
        if(have_ == total_) done_ = true;
        return 0u;
    }

    bool           Done() const { return done_; }
    const uint8_t* Blob() const { return buf_; }
    size_t         Size() const { return total_; }
    void           Reset() { have_ = 0; total_ = 0; done_ = false; }

private:
    /* no default initialisers: the module keeps this in SDRAM, which is not
       up until hw.Init(), so the owner calls Reset() first (or value-
       initialises it, {}) */
    uint8_t  buf_[kUserBlobMax];
    uint32_t have_, total_;
    bool     done_;
};

} // namespace kyk
