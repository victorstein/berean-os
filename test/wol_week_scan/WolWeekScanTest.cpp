#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "network/WolWeekScan.h"

namespace {

std::string loadFixture(const char* name) {
  const std::string path = std::string(WOL_FIXTURE_DIR) + "/" + name;
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << "missing fixture " << path;
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

void feedInChunks(WolWeekScanner& scanner, const std::string& body, const size_t chunk) {
  for (size_t offset = 0; offset < body.size(); offset += chunk) {
    scanner.feed(body.data() + offset, std::min(chunk, body.size() - offset));
  }
}

// The chunk sizes the transport actually produces, plus the two pathological
// ones: every publication link straddles a boundary at 1 and 7 bytes.
const std::vector<size_t> kChunkSizes = {1, 7, 4096};

}  // namespace

TEST(WolWeekScan, CurrentWeekYieldsBothPublications) {
  const std::string body = loadFixture("wol_2026_37.html");
  for (const size_t chunk : kChunkSizes) {
    WolWeekScanner scanner;
    feedInChunks(scanner, body, chunk);
    EXPECT_EQ(scanner.count(), 2) << "chunk size " << chunk;
    ASSERT_TRUE(scanner.has(MeetingPub::Watchtower)) << "chunk size " << chunk;
    ASSERT_TRUE(scanner.has(MeetingPub::Workbook)) << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Watchtower), "202607") << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Workbook), "202609") << "chunk size " << chunk;
  }
}

// Regression for templating the device's ISO year into the scan prefix: ISO week
// 2026/01 studies publications dated 2025.
TEST(WolWeekScan, FirstWeekOfYearReferencesPreviousYear) {
  const std::string body = loadFixture("wol_2026_01.html");
  for (const size_t chunk : kChunkSizes) {
    WolWeekScanner scanner;
    feedInChunks(scanner, body, chunk);
    ASSERT_TRUE(scanner.has(MeetingPub::Watchtower)) << "chunk size " << chunk;
    ASSERT_TRUE(scanner.has(MeetingPub::Workbook)) << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Watchtower), "202510") << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Workbook), "202511") << "chunk size " << chunk;
  }
}

// Memorial week publishes a Watchtower link and no workbook; that is a normal
// outcome, not a failed scan.
TEST(WolWeekScan, MemorialWeekHasNoWorkbook) {
  const std::string body = loadFixture("wol_2026_14.html");
  for (const size_t chunk : kChunkSizes) {
    WolWeekScanner scanner;
    feedInChunks(scanner, body, chunk);
    EXPECT_EQ(scanner.count(), 1) << "chunk size " << chunk;
    ASSERT_TRUE(scanner.has(MeetingPub::Watchtower)) << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Watchtower), "202601") << "chunk size " << chunk;
    EXPECT_FALSE(scanner.has(MeetingPub::Workbook)) << "chunk size " << chunk;
    EXPECT_STREQ(scanner.issue(MeetingPub::Workbook), "") << "chunk size " << chunk;
  }
}

TEST(WolWeekScan, EmptyBodyFindsNothing) {
  WolWeekScanner scanner;
  scanner.feed("", 0);
  EXPECT_EQ(scanner.count(), 0);
}

TEST(WolWeekScan, UnknownMonthIsRejected) {
  WolWeekScanner scanner;
  const std::string body = R"(<a href="/x/the-watchtower-2026/study-edition/smarch">)";
  scanner.feed(body.data(), body.size());
  EXPECT_FALSE(scanner.has(MeetingPub::Watchtower));
}

TEST(WolWeekScan, NonNumericYearIsRejected) {
  WolWeekScanner scanner;
  const std::string body = R"(<a href="/x/the-watchtower-simplified/study-edition/july">)";
  scanner.feed(body.data(), body.size());
  EXPECT_FALSE(scanner.has(MeetingPub::Watchtower));
}

TEST(WolWeekScan, FirstOccurrenceWins) {
  WolWeekScanner scanner;
  const std::string body = R"("the-watchtower-2026/study-edition/july" "the-watchtower-2027/study-edition/march")";
  scanner.feed(body.data(), body.size());
  ASSERT_TRUE(scanner.has(MeetingPub::Watchtower));
  EXPECT_STREQ(scanner.issue(MeetingPub::Watchtower), "202607");
}

TEST(WolWeekScan, ResetClearsPriorMatches) {
  const std::string body = loadFixture("wol_2026_37.html");
  WolWeekScanner scanner;
  scanner.feed(body.data(), body.size());
  ASSERT_EQ(scanner.count(), 2);
  scanner.reset();
  EXPECT_EQ(scanner.count(), 0);
  EXPECT_STREQ(scanner.issue(MeetingPub::Watchtower), "");
}

TEST(IsoWeek, MatchesKnownDates) {
  IsoWeek week;
  ASSERT_TRUE(isoWeekFromUtcDate(2026, 9, 12, week));
  EXPECT_EQ(week.year, 2026);
  EXPECT_EQ(week.week, 37);

  // 2026-01-01 is a Thursday, so it belongs to ISO week 2026/01.
  ASSERT_TRUE(isoWeekFromUtcDate(2026, 1, 1, week));
  EXPECT_EQ(week.year, 2026);
  EXPECT_EQ(week.week, 1);

  // 2027-01-01 is a Friday, so it falls in the last week of ISO year 2026.
  ASSERT_TRUE(isoWeekFromUtcDate(2027, 1, 1, week));
  EXPECT_EQ(week.year, 2026);
  EXPECT_EQ(week.week, 53);
}

TEST(IsoWeek, RejectsImpossibleDates) {
  IsoWeek week;
  EXPECT_FALSE(isoWeekFromUtcDate(2026, 0, 12, week));
  EXPECT_FALSE(isoWeekFromUtcDate(2026, 13, 12, week));
  EXPECT_FALSE(isoWeekFromUtcDate(2026, 9, 0, week));
  EXPECT_FALSE(isoWeekFromUtcDate(1970 - 1, 9, 12, week));
}

TEST(MeetingUrls, PadTheWeekAndCarryTheIssue) {
  EXPECT_EQ(meetingsPageUrl(IsoWeek{2026, 1}), "https://wol.jw.org/en/wol/meetings/r1/lp-e/2026/01");
  EXPECT_EQ(meetingsPageUrl(IsoWeek{2026, 37}), "https://wol.jw.org/en/wol/meetings/r1/lp-e/2026/37");

  EXPECT_EQ(pubMediaUrl(MeetingPub::Watchtower, "202607", "S"),
            "https://b.jw-cdn.org/apis/pub-media/"
            "GETPUBMEDIALINKS?output=json&pub=w&langwritten=S&fileformat=EPUB&issue=202607");
  EXPECT_EQ(pubMediaUrl(MeetingPub::Workbook, "202609", "S"),
            "https://b.jw-cdn.org/apis/pub-media/"
            "GETPUBMEDIALINKS?output=json&pub=mwb&langwritten=S&fileformat=EPUB&issue=202609");
}

TEST(MeetingUrls, ANonPeriodicalOmitsTheIssueParameterEntirely) {
  // Buscar downloads by symbol, and most of the catalog has no issue. The API
  // answers "lff" only when the parameter is absent; sending it empty is a
  // not-found.
  EXPECT_EQ(pubMediaUrlForSymbol("lff", "", "S"),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=lff&langwritten=S&fileformat=EPUB");
  EXPECT_EQ(pubMediaUrlForSymbol("lff", nullptr, "S"),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=lff&langwritten=S&fileformat=EPUB");
}

TEST(MeetingUrls, ASymbolWithAnIssueMatchesTheMeetingForm) {
  EXPECT_EQ(pubMediaUrlForSymbol("w", "202607", "S"), pubMediaUrl(MeetingPub::Watchtower, "202607", "S"))
      << "the meeting downloader and Buscar must resolve the same publication the same way";
}

TEST(MeetingUrls, TheAnyFormatFormDropsOnlyTheFileformatFilter) {
  EXPECT_EQ(pubMediaUrlForSymbol("km", "198001", "S", false),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=km&langwritten=S&issue=198001");
  EXPECT_EQ(pubMediaUrlForSymbol("lff", "", "S", false),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=lff&langwritten=S");
  EXPECT_EQ(pubMediaUrlForSymbol("w", "202607", "S", true), pubMediaUrlForSymbol("w", "202607", "S"))
      << "the EPUB-only form stays the default";
}

TEST(MeetingUrls, FilenameComesFromTheLastPathSegment) {
  EXPECT_EQ(filenameFromUrl("https://cfp2.jw-cdn.org/a/717b307/1/o/w_S_202607.epub"), "w_S_202607.epub");
  EXPECT_EQ(filenameFromUrl("mwb_S_202609.epub"), "mwb_S_202609.epub");
  EXPECT_EQ(filenameFromUrl("https://example.com/"), "");
  EXPECT_EQ(filenameFromUrl(""), "");
}

namespace {

CivilDate civil(const uint16_t year, const uint8_t month, const uint8_t day) {
  CivilDate date;
  date.year = year;
  date.month = month;
  date.day = day;
  return date;
}

IsoWeek isoWeek(const uint16_t year, const uint8_t number) {
  IsoWeek week;
  week.year = year;
  week.week = number;
  return week;
}

void expectDate(const CivilDate& actual, const int year, const int month, const int day) {
  EXPECT_EQ(actual.year, year);
  EXPECT_EQ(actual.month, month);
  EXPECT_EQ(actual.day, day);
}

}  // namespace

TEST(CivilCalendar, IsoWeekdayRunsMondayToSunday) {
  EXPECT_EQ(isoWeekday(civil(2026, 9, 28)), 1);
  EXPECT_EQ(isoWeekday(civil(2026, 9, 27)), 7);
  EXPECT_EQ(isoWeekday(civil(1970, 1, 1)), 4);
}

TEST(CivilCalendar, IsoWeekdayRejectsImpossibleDates) {
  EXPECT_EQ(isoWeekday(civil(2026, 2, 30)), 0);
  EXPECT_EQ(isoWeekday(civil(2026, 13, 1)), 0);
  EXPECT_EQ(isoWeekday(CivilDate{}), 0);
}

TEST(CivilCalendar, AddDaysCrossesMonthYearAndLeapDay) {
  expectDate(addDays(civil(2026, 9, 29), 5), 2026, 10, 4);
  expectDate(addDays(civil(2026, 12, 29), 5), 2027, 1, 3);
  expectDate(addDays(civil(2028, 2, 28), 1), 2028, 2, 29);
  expectDate(addDays(civil(2027, 1, 1), -1), 2026, 12, 31);
}

TEST(CivilCalendar, AddDaysGivesAnEmptyDateForAnImpossibleOne) { expectDate(addDays(civil(2026, 2, 30), 1), 0, 0, 0); }

TEST(IsoWeekMonday, DatesKnownWeeks) {
  CivilDate monday;
  ASSERT_TRUE(mondayOfIsoWeek(isoWeek(2026, 39), monday));
  expectDate(monday, 2026, 9, 21);
  // Week 1 of 2026 starts in the previous calendar year.
  ASSERT_TRUE(mondayOfIsoWeek(isoWeek(2026, 1), monday));
  expectDate(monday, 2025, 12, 29);
  ASSERT_TRUE(mondayOfIsoWeek(isoWeek(2026, 53), monday));
  expectDate(monday, 2026, 12, 28);
}

TEST(IsoWeekMonday, RoundTripsEveryWeekOf2025To2027) {
  for (uint16_t year = 2025; year <= 2027; ++year) {
    for (uint8_t number = 1; number <= 53; ++number) {
      CivilDate monday;
      if (!mondayOfIsoWeek(isoWeek(year, number), monday)) {
        // Only a year without a week 53 may refuse, and only that week.
        EXPECT_EQ(number, 53) << year;
        continue;
      }
      EXPECT_EQ(isoWeekday(monday), 1);
      IsoWeek back;
      ASSERT_TRUE(isoWeekFromUtcDate(monday.year, monday.month, monday.day, back));
      EXPECT_EQ(back.year, year);
      EXPECT_EQ(back.week, number);
    }
  }
}

TEST(IsoWeekMonday, RefusesAWeekTheYearDoesNotHave) {
  CivilDate monday;
  EXPECT_FALSE(mondayOfIsoWeek(isoWeek(2025, 53), monday));
  EXPECT_FALSE(mondayOfIsoWeek(isoWeek(2026, 0), monday));
  EXPECT_FALSE(mondayOfIsoWeek(isoWeek(2026, 54), monday));
}

TEST(LocalDate, ShiftsByTheClockOffset) {
  constexpr uint8_t UTC_MINUS_6 = 48 - 24;
  constexpr uint8_t UTC_PLUS_14 = 104;
  constexpr uint8_t NEPAL_PLUS_5_45 = 48 + 23;
  CivilDate local;

  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 23, 30, UTC_MINUS_6, local));
  expectDate(local, 2026, 9, 27);
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 28), 2, 0, UTC_MINUS_6, local));
  expectDate(local, 2026, 9, 27);
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 11, 0, UTC_PLUS_14, local));
  expectDate(local, 2026, 9, 28);
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 18, 15, NEPAL_PLUS_5_45, local));
  expectDate(local, 2026, 9, 28);
  ASSERT_TRUE(localDateFromUtc(civil(2027, 1, 1), 1, 0, UTC_MINUS_6, local));
  expectDate(local, 2026, 12, 31);
}

TEST(LocalDate, ClampsAnOffsetPastUtcPlus14) {
  CivilDate local;
  // Unclamped, 200 would be +38 h and move the date; clamped to +14 h it does not.
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 9, 0, 200, local));
  expectDate(local, 2026, 9, 27);
}

TEST(LocalDate, RejectsAnImpossibleTimeOrDate) {
  CivilDate local;
  EXPECT_FALSE(localDateFromUtc(civil(2026, 9, 27), 24, 0, 48, local));
  EXPECT_FALSE(localDateFromUtc(civil(2026, 9, 27), 12, 60, 48, local));
  EXPECT_FALSE(localDateFromUtc(civil(2026, 2, 30), 12, 0, 48, local));
}
