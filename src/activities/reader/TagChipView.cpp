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

void draw(UiAppHost::UiScreen& screen, const fui::Rect& rect, const char* label, const bool selected,
          const fui::ActionId action, const int16_t value, const uint16_t inputMask, const TagChips::Pad& pad) {
  const auto& theme = screen.theme();
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.normal.border = fui::Paint::solid(fui::Color::Black);
  styles.normal.borderWidth = 1;
  styles.normal.radius = static_cast<uint8_t>(std::min(rect.height / 2, 255));
  styles.selected = styles.normal;
  styles.selected.background = fui::Paint::solid(fui::Color::Black);
  styles.selected.foreground = fui::Paint::solid(fui::Color::White);
  // Focus/flash states keep the inverted pill instead of falling back to an unset style.
  styles.focused = styles.selected;
  styles.active = styles.selected;
  styles.disabled = styles.normal;

  fui::ButtonProps props;
  props.label = label;
  props.action = action;
  props.value = value;
  props.inputMask = inputMask;
  props.state = selected ? fui::StateSelected : fui::StateNormal;
  props.text = theme.smallText;
  props.text.bold = selected;
  props.styles = styles;
  // The chip already meets minTouchSize (TagChips::chipHeight); growing the hit rect would overlap
  // the neighbouring line, so the padding tiles the gaps instead.
  props.minTouchSize = 0;
  props.hitPadding = fui::Insets{static_cast<int16_t>(pad.top), static_cast<int16_t>(pad.right),
                                 static_cast<int16_t>(pad.bottom), static_cast<int16_t>(pad.left)};
  fui::button(screen.frame(), rect, props);
}

}  // namespace TagChipView
