#include "TagChipView.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "study/StudyStore.h"

namespace fui = freeink::ui;

namespace TagChipView {

void buildEntries(const std::optional<std::vector<size_t>>& scope, const std::optional<study::TagId> filter,
                  const size_t cap, std::vector<ChipEntry>& out) {
  const std::vector<study::TagId> activeIds = STUDY.palette().activeIds();
  const TagChips::Counts counts =
      scope ? TagChips::countIn(STUDY.passages(), *scope, activeIds) : TagChips::count(STUDY.passages(), activeIds);
  std::vector<TagChips::Candidate> picked;
  TagChips::candidates(counts, activeIds, filter, cap, picked);

  out.clear();
  out.reserve(picked.size());
  for (const TagChips::Candidate& candidate : picked) {
    ChipEntry chip;
    chip.kind = candidate.kind;
    switch (candidate.kind) {
      case TagChips::Kind::All:
        snprintf(chip.label, sizeof(chip.label), "%s %u", tr(STR_TAG_FILTER_ALL), static_cast<unsigned>(counts.all));
        break;
      case TagChips::Kind::Unlabelled:
        snprintf(chip.label, sizeof(chip.label), "%s %u", tr(STR_TAG_UNLABELLED),
                 static_cast<unsigned>(counts.unlabelled));
        break;
      case TagChips::Kind::Tag:
        chip.id = activeIds[candidate.slot];
        snprintf(chip.label, sizeof(chip.label), "%s %u", STUDY.tagName(chip.id).c_str(),
                 static_cast<unsigned>(counts.perTag[candidate.slot]));
        break;
      case TagChips::Kind::NewTag:
        // candidates never emits one; skipping it cannot show an empty chip.
        continue;
    }
    out.push_back(chip);
  }
}

bool isSelected(const ChipEntry& chip, const std::optional<study::TagId> filter) {
  switch (chip.kind) {
    case TagChips::Kind::All:
      return !filter;
    case TagChips::Kind::Unlabelled:
      return filter == study::UNLABELLED;
    case TagChips::Kind::Tag:
      return filter == chip.id;
    case TagChips::Kind::NewTag:
      return false;
  }
  return false;
}

Metrics metricsFor(UiAppHost::UiScreen& screen) {
  const auto& theme = screen.theme();
  Metrics metrics;
  metrics.padX = theme.spaceLg;
  metrics.gap = theme.spaceSm;
  metrics.chipHeight =
      TagChips::chipHeight(screen.target().lineHeight(theme.smallText.font), theme.spaceMd, theme.minTouchSize);
  return metrics;
}

int measure(UiAppHost::UiScreen& screen, const char* label, const Metrics& metrics) {
  fui::TextStyle style = screen.theme().smallText;
  style.bold = true;
  const int textWidth = screen.target().measureText(style.font, label, style).width;
  return std::max(textWidth + 2 * metrics.padX, metrics.chipHeight);
}

void draw(UiAppHost::UiScreen& screen, const fui::Rect& rect, const char* label, const TagChips::ChipState state,
          const fui::ActionId action, const int16_t value, const uint16_t inputMask, const TagChips::Pad& pad) {
  const auto& theme = screen.theme();
  fui::StyleSet styles;
  styles.explicitlySet = true;
  fui::BoxStyle& look = styles.normal;
  look.background = fui::Paint::solid(fui::Color::White);
  look.foreground = fui::Paint::solid(fui::Color::Black);
  look.border = fui::Paint::solid(fui::Color::Black);
  look.borderWidth = 1;
  look.radius = static_cast<uint8_t>(std::min(rect.height / 2, 255));
  switch (state) {
    case TagChips::ChipState::Inactive:
    case TagChips::ChipState::Action:
      break;
    case TagChips::ChipState::Active:
      look.background = fui::Paint::solid(fui::Color::Black);
      look.foreground = fui::Paint::solid(fui::Color::White);
      break;
    case TagChips::ChipState::Disabled:
      look.background = fui::Paint::dither(fui::Color::LightGray);
      look.border = fui::Paint::none();
      look.borderWidth = 0;
      break;
  }
  // A tap flash paints the focused style; every slot keeps the chip's own look so a toggled chip is
  // never repainted in the state it just left.
  styles.selected = look;
  styles.focused = look;
  styles.active = look;
  styles.disabled = look;

  fui::ButtonProps props;
  props.label = label;
  props.action = action;
  props.value = value;
  props.inputMask = inputMask;
  // Never fui::StateDisabled: findTouch skips such a hit for every input, so a Disabled chip's tap and
  // long-press would never arrive.
  props.state = state == TagChips::ChipState::Active ? fui::StateSelected : fui::StateNormal;
  props.text = theme.smallText;
  props.text.bold = state == TagChips::ChipState::Active || state == TagChips::ChipState::Action;
  props.styles = styles;
  // The chip already meets minTouchSize (TagChips::chipHeight); growing the hit rect would overlap
  // the neighbouring line, so the padding tiles the gaps instead.
  props.minTouchSize = 0;
  props.hitPadding = fui::Insets{static_cast<int16_t>(pad.top), static_cast<int16_t>(pad.right),
                                 static_cast<int16_t>(pad.bottom), static_cast<int16_t>(pad.left)};
  fui::button(screen.frame(), rect, props);
}

}  // namespace TagChipView
