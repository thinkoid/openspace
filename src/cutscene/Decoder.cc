// -*- mode: c++; -*-

#include <boost/thread/sync_bounded_queue.hpp>
#include <memory>

#include "cutscene/Decoder.hh"
#include "assert/assert.hh"

using namespace boost::concurrent;

namespace cutscene {

FrameSize::FrameSize (size_t in_width, size_t in_height, size_t in_stride)
    : width (in_width), height (in_height), stride (in_stride)
{ }

Decoder::Decoder () : m_decoding (true) {}

Decoder::~Decoder () {}

bool Decoder::tryPopAudioData (AudioFramePtr& out) {
    auto res = m_audioQueue->try_pull_front (out);
    return res == queue_op_status::success;
}

bool Decoder::tryPopSubtitleData (SubtitleFramePtr& out) {
    auto res = m_subtitleQueue->try_pull_front (out);
    return res == queue_op_status::success;
}

bool Decoder::tryPopVideoFrame (VideoFramePtr& out) {
    auto res = m_videoQueue->try_pull_front (out);
    return res == queue_op_status::success;
}

void Decoder::initializeQueues (size_t queueSize) {
    m_queueSize = queueSize;

    m_videoQueue.reset (new sync_bounded_queue< VideoFramePtr > (m_queueSize));
    m_audioQueue.reset (new sync_bounded_queue< AudioFramePtr > (m_queueSize));
    m_subtitleQueue.reset (
        new sync_bounded_queue< SubtitleFramePtr > (m_queueSize));
}

void Decoder::stopDecoder () {
    m_decoding = false;

    m_videoQueue->close ();
    m_audioQueue->close ();
    m_subtitleQueue->close ();
}

bool Decoder::canPushAudioData () { return !isAudioQueueFull (); }

void Decoder::pushAudioData (AudioFramePtr&& data) {
    ASSERTX (data, "Invalid audio data passed!");

    try {
        m_audioQueue->push_back (std::move (data));
    }
    catch (sync_queue_is_closed&) {
        // Ignore
    }
}

bool Decoder::canPushSubtitleData () { return !isSubtitleQueueFull (); }
void Decoder::pushSubtitleData (SubtitleFramePtr&& data) {
    ASSERTX (data, "Invalid audio data passed!");

    try {
        m_subtitleQueue->push_back (std::move (data));
    }
    catch (sync_queue_is_closed&) {
        // Ignore
    }
}

bool Decoder::canPushVideoData () { return !isVideoQueueFull (); }

void Decoder::pushFrameData (VideoFramePtr&& frame) {
    ASSERTX (frame, "Invalid video data passed!");

    try {
        m_videoQueue->push_back (std::move (frame));
    }
    catch (sync_queue_is_closed&) {
        // Ignore
    }
}
} // namespace cutscene
