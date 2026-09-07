#ifndef AKIRA_CONTROLLER_PICKER_VIEW_HPP
#define AKIRA_CONTROLLER_PICKER_VIEW_HPP

#include <borealis.hpp>
#include <switch.h>

#include <functional>
#include <vector>

#include "input/pad_path.hpp"

class InputManager;

/*
 * Which controller the stream should read.
 *
 * Shown before anything starts, so there is no clock running and nobody has to
 * choose in a hurry - and because the alternative, guessing, is what left gyro
 * reading from Joy-Cons in the rail while a Pro Controller was in your hands.
 *
 * You choose by *using* the pad rather than by picking it off a list. That
 * sidesteps the obvious trap - if the wrong pad were already selected you could
 * not navigate the list that would fix it - and it is faster than reading
 * labels when what you actually want is "this one, the one I am holding".
 *
 * Nothing is remembered. The choice lasts for the session, so there is no
 * stored answer to go stale, and the sysmodule's own record stays the overlay's
 * business.
 */
class ControllerPickerView : public brls::Box {
public:
    using OnChosen = std::function<void(HidNpadIdType)>;

    /* Re-asked every half second so the screen tracks what is actually
     * connected. Supplied by the caller because the picker has no business
     * knowing about InputManager. */
    using Describe = std::function<std::vector<akira::input::PadDescription>()>;

    /* Continue without naming a pad: the caller keeps whatever default was
     * already resolved, which is what happened before this screen existed. */
    static constexpr HidNpadIdType kNoChoice  = static_cast<HidNpadIdType>(0xFF);

    /* Backed out. Nothing has started yet, so the caller should stand the
     * whole attempt down rather than stream on a pad nobody picked. */
    static constexpr HidNpadIdType kCancelled = static_cast<HidNpadIdType>(0xFE);

    /*
     * Whether asking is worth it: more than one connected pad that could
     * plausibly be the one in your hands. One pad, or several that are
     * genuinely interchangeable, is not a question worth putting on screen.
     */
    static bool worthAsking(const std::vector<akira::input::PadDescription>& pads);

    ControllerPickerView(std::vector<akira::input::PadDescription> pads,
                         OnChosen onChosen, Describe describe);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

private:
    void choose(std::size_t index);

    /* Leave without choosing. The stream proceeds on whatever the
     * default resolution picks, which is what happened before this
     * screen existed - never a dead end. */
    void cancel();

    /* Rebuild the row from a fresh enumeration, carrying the selection across
     * where the same controller is still present. */
    void rebuild(std::vector<akira::input::PadDescription> fresh);

    /* Which entry in the new list is the one that was selected in the old, or
     * -1 if it has gone. */
    int carryOverSelection(bool hadSelection, HidNpadIdType selNpad,
                           akira::input::PadPathKind selKind,
                           uint16_t selVid, uint16_t selPid) const;
    void buildCards();
    static bool SameSet(const std::vector<akira::input::PadDescription>& a,
                        const std::vector<akira::input::PadDescription>& b);

    /* Proceed on the default pad. Separate from cancel() because B has
     * meant "back" on every other screen in this app and should not
     * quietly start a stream here. */
    void accept();

    std::vector<akira::input::PadDescription> m_pads;
    std::vector<PadState>                     m_states;
    std::vector<brls::Box*>                   m_cards;
    /* Index of the chosen pad, or -1. Single selection for now;
     * this becomes a set when several pads can drive a stream. */
    int                                       m_selected = -1;
    std::vector<bool>                         m_seen_clear;
    unsigned                                  m_frames = 0;
    OnChosen                                  m_onChosen;
    Describe                                  m_describe;
    brls::Box*                                m_row = nullptr;
    bool                                      m_done = false;
};

#endif // AKIRA_CONTROLLER_PICKER_VIEW_HPP
