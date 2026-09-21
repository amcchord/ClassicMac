/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-23 divingkatae and maximum
                      (theweirdo)     spatium

(Contact divingkatae#1017 or powermax#2286 on Discord for more info)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <devices/common/dmacore.h>
#include <core/timermanager.h>
#include <devices/sound/soundserver.h>
#include <loguru.hpp>
#include <functional>
#include <core/endianswap.h>


typedef enum {
    SND_SERVER_DOWN = 0,
    SND_SERVER_STARTED,
    SND_SERVER_STREAM_OPENED,
    SND_SERVER_STREAM_STARTED,
} Status;

class SoundServer::Impl {
public:
    Status status = Status::SND_SERVER_DOWN;
    uint32_t poll_timer = 0;
    std::function<void()> poll_cb;
    std::unique_ptr<uint8_t[]> sound_buffer;
};


SoundServer::SoundServer(): impl(std::make_unique<Impl>())
{
}

SoundServer::~SoundServer()
{
    this->shutdown();
}

int SoundServer::start()
{
    impl->status = SND_SERVER_STARTED;
    return 0;
}

void SoundServer::shutdown()
{
    switch (impl->status) {
    case SND_SERVER_STREAM_STARTED:
    case SND_SERVER_STREAM_OPENED:
        close_out_stream();
        break;
    case SND_SERVER_STARTED:
    case SND_SERVER_DOWN:
        // nothing to do
        break;
    }

    impl->status = SND_SERVER_DOWN;

    LOG_F(INFO, "Sound Server shut down.");
}

static void poll_sound(uint8_t *sound_buffer, int sound_buffer_size, DmaOutChannel *dma_ch) {
    if (!dma_ch->is_out_active()) {
        return;
    }

    // Drain on the emulation thread in bounded blocks, even without a host
    // audio device. Copland waits for DBDMA completion during startup.
    int req_size = sound_buffer_size;
    int out_size = 0;

    while (req_size > 0) {
        uint8_t *chunk;
        uint32_t chunk_size;
        if (!dma_ch->pull_data(req_size, &chunk_size, &chunk)) {
            if (!chunk_size || chunk_size > uint32_t(req_size)) break;
            std::copy(chunk, chunk + chunk_size, sound_buffer + out_size);
            req_size -= chunk_size;
            out_size += chunk_size;
        } else {
            break;
        }
    }

}

int SoundServer::open_out_stream(uint32_t sample_rate, DmaOutChannel *dma_ch)
{
    uint32_t sample_size = 16;
    uint32_t channels = 2;

    // Retain the 384-frame, 1 ms guest timer cadence of the working wasm port.
    // This backend intentionally supplies no host audio.
    int audio_frames_per_block = 384;
    int sound_buffer_size = (sample_size >> 3) * channels * audio_frames_per_block;
    impl->sound_buffer = std::make_unique<uint8_t[]>(sound_buffer_size);

    impl->poll_cb = std::bind(poll_sound, impl->sound_buffer.get(), sound_buffer_size, dma_ch);
    impl->status = SND_SERVER_STREAM_OPENED;


    return 0;
}

int SoundServer::start_out_stream()
{
    if (impl->status == SND_SERVER_STREAM_STARTED) return 0;
    impl->poll_timer = TimerManager::get_instance()->add_cyclic_timer(MSECS_TO_NSECS(1), impl->poll_cb);
    impl->status = SND_SERVER_STREAM_STARTED;

    return 0;
}

void SoundServer::close_out_stream()
{
    if (impl->status == SND_SERVER_STREAM_STARTED) {
        TimerManager::get_instance()->cancel_timer(impl->poll_timer);
    }
    impl->status = SND_SERVER_STARTED;

}
