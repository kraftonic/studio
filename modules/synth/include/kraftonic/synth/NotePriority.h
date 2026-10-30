// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace kraftonic::synth
{
enum class Priority
{
    last,
    low,
    high
};
enum class Retrigger
{
    always,
    legatoOnly,
    never
};
struct NoteChange
{
    int note = -1;
    bool changed = false, legato = false, retrigger = false;
};
template <std::size_t Capacity = 32> class NotePriority
{
  public:
    void setPriority (Priority p) noexcept
    {
        priority_ = p;
    }
    void setRetrigger (Retrigger p) noexcept
    {
        retrigger_ = p;
    }
    NoteChange noteOn (int note) noexcept
    {
        const int before = current_;
        std::size_t i = 0;
        while (i < count_ && notes_[i] != note)
            ++i;
        if (i < count_)
        {
            for (std::size_t j = i + 1; j < count_; ++j)
                notes_[j - 1] = notes_[j];
            --count_;
        }
        if (count_ < Capacity)
            notes_[count_++] = note;
        current_ = choose ();
        const bool legato = before >= 0;
        return {current_, current_ != before, legato,
                retrigger_ == Retrigger::always || (retrigger_ == Retrigger::legatoOnly && !legato)};
    }
    NoteChange noteOff (int note) noexcept
    {
        const int before = current_;
        for (std::size_t i = 0; i < count_; ++i)
            if (notes_[i] == note)
            {
                for (std::size_t j = i + 1; j < count_; ++j)
                    notes_[j - 1] = notes_[j];
                --count_;
                break;
            }
        current_ = choose ();
        return {current_, current_ != before, before >= 0 && current_ >= 0, false};
    }
    void reset () noexcept
    {
        count_ = 0;
        current_ = -1;
    }
    int current () const noexcept
    {
        return current_;
    }
    std::size_t size () const noexcept
    {
        return count_;
    }

  private:
    int choose () const noexcept
    {
        if (!count_)
            return -1;
        if (priority_ == Priority::last)
            return notes_[count_ - 1];
        int n = notes_[0];
        for (std::size_t i = 1; i < count_; ++i)
            n = priority_ == Priority::low ? (notes_[i] < n ? notes_[i] : n) : (notes_[i] > n ? notes_[i] : n);
        return n;
    }
    std::array<int, Capacity> notes_{};
    std::size_t count_ = 0;
    int current_ = -1;
    Priority priority_ = Priority::last;
    Retrigger retrigger_ = Retrigger::always;
};
} // namespace kraftonic::synth
