// SPDX-License-Identifier: MIT
#pragma once

// ============================================================================
//  Sample-accurate parameter schedule.
//
//  SDK-free. The adapter copies each host parameter queue into one "lane"
//  (points already sorted by offset, as VST3 and CLAP guarantee), then walks the
//  block in sub-blocks: before each sub-block it applies every point whose
//  offset has been reached, so a change at sample 300 lands at sample 300
//  (or, for points closer than kMinSubBlock to the previous split, within
//  kMinSubBlock samples). The lanes are merged with per-lane cursors, so no
//  sort and no allocation: storage is fixed at construction.
//
//  Overflow never allocates and never loses the final value: when the point
//  store is full, a new point overwrites the lane's last point (value and
//  offset), so the lane still ends on the host's last value.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace kraftonic::formats
{

struct ParamPoint
{
    std::int32_t offset = 0;
    std::uint32_t id = 0;
    float value = 0.0F; // caller-defined (the effect adapter stores plain units)
};

template <std::size_t MaxPoints = 2048, std::size_t MaxLanes = 128>
class ParamSchedule
{
public:
    /** Smallest sub-block the walker produces (except at the block end). */
    static constexpr int kMinSubBlock = 16;
    static constexpr int kNoMorePoints = std::numeric_limits<int>::max();

    void clear() noexcept
    {
        numPoints_ = 0;
        numLanes_ = 0;
    }

    /** Starts a new lane; returns false (and ignores its points) when full. */
    bool beginLane() noexcept
    {
        if (numLanes_ >= MaxLanes)
        {
            laneOpen_ = false;
            return false;
        }
        Lane& lane = lanes_[numLanes_++];
        lane.begin = numPoints_;
        lane.end = numPoints_;
        lane.cursor = numPoints_;
        laneOpen_ = true;
        return true;
    }

    /** Adds a point to the current lane; offsets are clamped to [0, numSamples-1]
        and made non-decreasing within the lane. */
    void push (std::int32_t offset, std::uint32_t id, float value, int numSamples) noexcept
    {
        if (!laneOpen_ || numLanes_ == 0)
            return;
        Lane& lane = lanes_[numLanes_ - 1];
        std::int32_t clamped = offset < 0 ? 0 : offset;
        if (numSamples > 0 && clamped > numSamples - 1)
            clamped = numSamples - 1;
        if (lane.end > lane.begin && clamped < points_[lane.end - 1].offset)
            clamped = points_[lane.end - 1].offset;
        if (numPoints_ >= MaxPoints)
        {
            if (lane.end > lane.begin)
                points_[lane.end - 1] = ParamPoint {clamped, id, value};
            return;
        }
        points_[numPoints_++] = ParamPoint {clamped, id, value};
        lane.end = numPoints_;
    }

    /** Offset of the earliest point not yet applied, or kNoMorePoints. */
    int nextOffset() const noexcept
    {
        int next = kNoMorePoints;
        for (std::size_t l = 0; l < numLanes_; ++l)
        {
            const Lane& lane = lanes_[l];
            if (lane.cursor < lane.end && points_[lane.cursor].offset < next)
                next = points_[lane.cursor].offset;
        }
        return next;
    }

    /** Applies (in lane order) every pending point with offset <= limit.
        Returns true when at least one point was applied. */
    template <typename Apply>
    bool applyUpTo (int limit, Apply&& apply) noexcept
    {
        bool any = false;
        for (std::size_t l = 0; l < numLanes_; ++l)
        {
            Lane& lane = lanes_[l];
            while (lane.cursor < lane.end && points_[lane.cursor].offset <= limit)
            {
                const ParamPoint& p = points_[lane.cursor++];
                apply (p.id, p.value);
                any = true;
            }
        }
        return any;
    }

    /** End of the sub-block that starts at `start` in a block of `numSamples`. */
    int subBlockEnd (int start, int numSamples) const noexcept
    {
        const int next = nextOffset();
        int end = next < numSamples ? next : numSamples;
        if (end < start + kMinSubBlock)
            end = start + kMinSubBlock;
        return end < numSamples ? end : numSamples;
    }

    std::size_t numPoints() const noexcept { return numPoints_; }
    std::size_t numLanes() const noexcept { return numLanes_; }

private:
    struct Lane
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t cursor = 0;
    };

    std::array<ParamPoint, MaxPoints> points_ {};
    std::array<Lane, MaxLanes> lanes_ {};
    std::size_t numPoints_ = 0;
    std::size_t numLanes_ = 0;
    bool laneOpen_ = false;
};

} // namespace kraftonic::formats
