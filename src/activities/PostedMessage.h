#pragma once

class GfxRenderer;

// A popup shown over the next screen that finishes rendering, instead of drawn
// immediately. Every caller of an immediate popup went on to request a render
// or finish(), and that render painted straight over it -- on e-ink, a flash
// too brief to read. A refusal the user cannot see is a silent failure.
//
// Posted from the main task, drawn from the render task. Holds two messages so
// two notices raised together (both study stores failing to load) show one
// after the other instead of the second replacing the first.
namespace PostedMessage {

// `message` must outlive the next render: a tr() string or a literal.
void post(const char* message);

// Called by a render path after its own displayBuffer. Draws the message being
// shown, redrawing it on every render until PostedMessageQueue::MIN_DISPLAY_MS
// has passed, then the next posted one. A message identical to one queued or on
// screen is not posted twice.
void drawNext(const GfxRenderer& renderer);

}  // namespace PostedMessage
