#include "PreachingContent.h"

#include <I18n.h>
#include <Logging.h>
#include <Preaching/IntroSection.h>

#include <cstdio>

#include "PreachingSession.h"
#include "activities/reader/SpineHtmlStream.h"
#include "study/PsramJsonAllocator.h"

namespace PreachingContent {
namespace {

constexpr const char* MODULE = "PREACH";

bool feedParser(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<preaching::QuestionParser*>(ctx)->feed(chunk, length, isFinal);
}

preaching::CardKind cardKindOf(const preaching::BlockKind kind) {
  switch (kind) {
    case preaching::BlockKind::Heading:
      return preaching::CardKind::Heading;
    case preaching::BlockKind::Verse:
      return preaching::CardKind::Verse;
    case preaching::BlockKind::Text:
      return preaching::CardKind::Text;
  }
  return preaching::CardKind::Text;
}

void buildQuestionCards(const PreachingSession& session, TopicCards& out) {
  const auto& blocks = out.parsed.blocks;
  const int next = preaching::IntroSection::nextQuestion(out.question);
  out.cards.reserve(blocks.size() + 2);
  out.verseSource.reserve(blocks.size());
  if (!out.parsed.title.empty()) out.cards.push_back({preaching::CardKind::Title, out.parsed.title, {}});
  for (size_t index = 0; index < blocks.size(); ++index) {
    const preaching::Block& block = blocks[index];
    const preaching::CardKind kind = cardKindOf(block.kind);
    const std::string_view reference =
        kind == preaching::CardKind::Verse ? std::string_view(block.reference) : std::string_view();
    out.cards.push_back({kind, block.text, reference});
    if (kind == preaching::CardKind::Verse) out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  if (next != 0) {
    out.cards.push_back({preaching::CardKind::NextVisit, tr(STR_PREACHING_NEXT_VISIT), session.questionLabel(next)});
  }
}

}  // namespace

LoadResult loadQuestion(const PreachingSession& session, GfxRenderer& renderer, const int question, TopicCards& out) {
  if (!session.sectionFound() || session.spine(question) < 0) {
    LOG_ERR(MODULE, "Question %d is not in this Bible", question);
    return LoadResult::Unreadable;
  }
  preaching::QuestionParser parser;
  if (!parser.valid()) return LoadResult::OutOfMemory;
  const bool streamed =
      SpineHtmlStream::stream(session.book(), session.spine(question), renderer, &feedParser, &parser);
  if (parser.outOfMemory()) return LoadResult::OutOfMemory;
  if (!streamed || !parser.take(out.parsed)) {
    LOG_ERR(MODULE, "Question %d did not read or parse", question);
    return LoadResult::Unreadable;
  }

  out.kind = PreachingOrigin::Kind::Question;
  out.question = question;
  char header[48];
  snprintf(header, sizeof(header), tr(STR_PREACHING_QUESTION_N), question);
  out.header = header;
  buildQuestionCards(session, out);
  return LoadResult::Ok;
}

LoadResult loadTag(const PreachingSession& session, const study::TagId tag, TopicCards& out) {
  const PreachingPassages::Status status =
      PreachingPassages::passagesWithTag(tag, PsramJsonAllocator::passageDoc(), out.passages);
  switch (status) {
    case PreachingPassages::Status::Ok:
      break;
    case PreachingPassages::Status::OutOfMemory:
      return LoadResult::OutOfMemory;
    case PreachingPassages::Status::Missing:
    case PreachingPassages::Status::Unreadable:
      return LoadResult::VersesUnavailable;
  }

  out.kind = PreachingOrigin::Kind::Tag;
  out.tag = tag;
  out.header = session.topicName(tag);
  out.cards.reserve(out.passages.size());
  out.verseSource.reserve(out.passages.size());
  for (size_t index = 0; index < out.passages.size(); ++index) {
    const PreachingPassages::Passage& passage = out.passages[index];
    out.cards.push_back({preaching::CardKind::Verse, passage.text.view(), passage.reference});
    out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  return LoadResult::Ok;
}

const char* failureMessage(const LoadResult result) {
  return result == LoadResult::VersesUnavailable ? tr(STR_PREACHING_VERSES_UNREADABLE) : tr(STR_PREACHING_OPEN_FAILED);
}

}  // namespace PreachingContent
