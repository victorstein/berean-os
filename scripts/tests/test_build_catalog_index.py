import os
import sys
import tempfile
import unittest
from datetime import date, timedelta
from pathlib import Path

# discover -s scripts/tests puts only this directory on sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import build_catalog_index as builder  # noqa: E402

TODAY = date(2026, 10, 5)
G = ("g", "19800422")
KM = ("km", "198001")
LFF = ("lff", "")
ROWS = [
    ("g", "19800422", "1980", "periodical", "¡Despertad! 1980"),
    ("km", "198001", "1980", "periodical", "Nuestro Servicio del Reino 1980"),
    ("lff", "", "2021", "book", "Disfrute de la vida para siempre"),
]
EPUB = (200, b'{"files":{"S":{"EPUB":[{"file":{"url":"https://cdn/x.epub"}}]}}}')
JWPUB_ONLY = (200, b'{"files":{"S":{"JWPUB":[{"file":{"url":"https://cdn/x.jwpub"}}]}}}')
NOT_FOUND = (404, b"")


class FakeClock:
    """Time moves only when the code under test sleeps, or a request takes its latency."""

    def __init__(self, latency=0.1):
        self.now = 0.0
        self.latency = latency

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeApi:
    def __init__(self, clock, answers=None, default=EPUB):
        self.clock = clock
        self.answers = answers or {}
        self.default = default
        self.urls = []
        self.starts = []

    def get(self, url):
        self.urls.append(url)
        self.starts.append(self.clock.now)
        self.clock.now += self.clock.latency
        for symbol, answer in self.answers.items():
            if f"pub={symbol}&" in url:
                return answer
        return self.default


def probe(keys, api, clock, max_probes=1000, interval=1.0, budget=1500.0):
    return builder.run_probes(keys, "S", api.get, clock.clock, clock.sleep, interval, max_probes, budget)


class PlanTest(unittest.TestCase):
    def test_only_uncached_rows_are_planned(self):
        cache = {G: ("1", TODAY - timedelta(days=400)), KM: ("0", TODAY - timedelta(days=10))}
        self.assertEqual(builder.plan_probes(ROWS, cache, TODAY), [LFF])

    def test_a_positive_is_never_probed_again(self):
        cache = {key: ("1", TODAY - timedelta(days=1000)) for key in (G, KM, LFF)}
        self.assertEqual(builder.plan_probes(ROWS, cache, TODAY), [])

    def test_a_negative_is_probed_again_only_after_ninety_days(self):
        cache = {
            G: ("0", TODAY - timedelta(days=90)),
            KM: ("0", TODAY - timedelta(days=91)),
            LFF: ("1", TODAY),
        }
        self.assertEqual(builder.plan_probes(ROWS, cache, TODAY), [KM])

    def test_unprobed_rows_come_first_then_due_negatives_oldest_first(self):
        cache = {G: ("0", TODAY - timedelta(days=200)), KM: ("0", TODAY - timedelta(days=300))}
        self.assertEqual(builder.plan_probes(ROWS, cache, TODAY), [LFF, KM, G])


class ProbeTest(unittest.TestCase):
    def test_the_probe_is_the_request_the_device_makes(self):
        self.assertEqual(
            builder.probe_url("w", "202607", "S"),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS"
            "?output=json&pub=w&langwritten=S&fileformat=EPUB&issue=202607",
        )
        self.assertEqual(
            builder.probe_url("lff", "", "S"),
            "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=lff&langwritten=S&fileformat=EPUB",
        )

    def test_only_an_epub_or_a_404_is_an_answer(self):
        self.assertEqual(builder.classify(*EPUB, "S"), "1")
        self.assertEqual(builder.classify(*NOT_FOUND, "S"), "0")
        self.assertIsNone(builder.classify(*JWPUB_ONLY, "S"))
        self.assertIsNone(builder.classify(200, b"not json", "S"))
        self.assertIsNone(builder.classify(200, b"[]", "S"))
        self.assertIsNone(builder.classify(500, b"", "S"))
        self.assertIsNone(builder.classify(None, b"", "S"))

    def test_probes_are_at_least_the_interval_apart(self):
        clock = FakeClock()
        api = FakeApi(clock)
        probe([G, KM, LFF], api, clock)
        self.assertEqual(len(api.starts), 3)
        for earlier, later in zip(api.starts, api.starts[1:]):
            self.assertGreaterEqual(later - earlier, 1.0 - 1e-9)

    def test_stops_at_the_probe_cap(self):
        clock = FakeClock()
        api = FakeApi(clock)
        results, throttled = probe([G, KM, LFF], api, clock, max_probes=2)
        self.assertEqual(len(api.urls), 2)
        self.assertFalse(throttled)

    def test_stops_at_the_time_budget(self):
        clock = FakeClock()
        api = FakeApi(clock)
        probe([G, KM, LFF], api, clock, budget=1.5)
        self.assertEqual(len(api.urls), 2)

    def test_answers_are_recorded_and_non_answers_are_not(self):
        clock = FakeClock()
        api = FakeApi(clock, answers={"km": NOT_FOUND, "lff": (None, b"")})
        results, throttled = probe([G, KM, LFF], api, clock)
        self.assertEqual(results, {G: "1", KM: "0"})
        self.assertFalse(throttled)

    def test_a_429_stops_the_run(self):
        clock = FakeClock()
        api = FakeApi(clock, answers={"km": (429, b"")})
        results, throttled = probe([G, KM, LFF], api, clock)
        self.assertEqual(results, {G: "1"})
        self.assertTrue(throttled)
        self.assertEqual(len(api.urls), 2)

    def test_a_5xx_stops_the_run(self):
        clock = FakeClock()
        api = FakeApi(clock, answers={"g": (503, b"")})
        results, throttled = probe([G, KM, LFF], api, clock)
        self.assertEqual(results, {})
        self.assertTrue(throttled)


class FormatTest(unittest.TestCase):
    def test_v1_is_the_original_layout_byte_for_byte(self):
        self.assertEqual(
            builder.render_index(ROWS, 1, "S", "m-1", "2026-10-05"),
            "berean-catalog\t1\tS\tm-1\t2026-10-05\n"
            "g\t19800422\t1980\tperiodical\t¡Despertad! 1980\n"
            "km\t198001\t1980\tperiodical\tNuestro Servicio del Reino 1980\n"
            "lff\t\t2021\tbook\tDisfrute de la vida para siempre\n",
        )

    def test_v2_writes_the_flag_before_the_title(self):
        cache = {G: ("1", TODAY), KM: ("0", TODAY)}
        self.assertEqual(
            builder.render_index(ROWS, 2, "S", "m-1", "2026-10-05", cache),
            "berean-catalog\t2\tS\tm-1\t2026-10-05\n"
            "g\t19800422\t1980\tperiodical\t1\t¡Despertad! 1980\n"
            "km\t198001\t1980\tperiodical\t0\tNuestro Servicio del Reino 1980\n"
            "lff\t\t2021\tbook\t\tDisfrute de la vida para siempre\n",
        )

    def test_the_probe_cache_round_trips_and_drops_rows_no_longer_in_the_catalog(self):
        cache = {G: ("1", TODAY), ("gone", "20010101"): ("0", TODAY)}
        text = builder.render_probes(cache, ROWS, "S")
        self.assertEqual(text, "berean-probes\t1\tS\ng\t19800422\t1\t2026-10-05\n")
        self.assertEqual(builder.read_probes(text, "S"), {G: ("1", TODAY)})

    def test_a_probe_cache_for_another_language_is_refused(self):
        with self.assertRaises(SystemExit):
            builder.read_probes("berean-probes\t1\tE\n", "S")

    def test_a_malformed_probe_cache_is_refused(self):
        with self.assertRaises(SystemExit):
            builder.read_probes("berean-probes\t1\tS\ng\t19800422\tmaybe\t2026-10-05\n", "S")
        with self.assertRaises(SystemExit):
            builder.read_probes("berean-probes\t1\tS\ng\t19800422\t1\tyesterday\n", "S")

    def test_seeding_from_v2_keeps_every_flag_and_dates_it_today(self):
        old = TODAY - timedelta(days=50)
        v2 = builder.render_index(ROWS, 2, "S", "m-1", "2026-08-16", {G: ("1", old), KM: ("0", old)})
        self.assertEqual(builder.seed_from_v2(v2, "S", TODAY), {G: ("1", TODAY), KM: ("0", TODAY)})

    def test_seeding_refuses_anything_but_a_v2_index_for_the_language(self):
        with self.assertRaises(SystemExit):
            builder.seed_from_v2(builder.render_index(ROWS, 1, "S", "m", "2026-10-05"), "S", TODAY)
        with self.assertRaises(SystemExit):
            builder.seed_from_v2(builder.render_index(ROWS, 2, "E", "m", "2026-10-05"), "S", TODAY)


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def path(self, name):
        return os.path.join(self.tmp.name, name)

    def write(self, name, text):
        with open(self.path(name), "w", encoding="utf-8") as handle:
            handle.write(text)
        return self.path(name)

    def read(self, name):
        with open(self.path(name), encoding="utf-8") as handle:
            return handle.read()

    def args(self, *extra):
        return builder.parse_args(
            ["--language-code", "S", "--out", self.path("v2.txt"), "--out-v1", self.path("v1.txt"),
             "--probes-out", self.path("probes.tsv"), *extra]
        )

    def build(self, args, answers=None):
        clock = FakeClock()
        api = FakeApi(clock, answers=answers)
        status = builder.build(args, ROWS, "m-1", TODAY, api.get, clock.clock, clock.sleep)
        return status, api

    def published(self, cache):
        """Writes last week's v1, v2 and probe cache as the workflow would have fetched them."""
        return (
            "--previous", self.write("prev-v2.txt", builder.render_index(ROWS, 2, "S", "m-1", "2026-09-28", cache)),
            "--previous-v1", self.write("prev-v1.txt", builder.render_index(ROWS, 1, "S", "m-1", "2026-09-28")),
            "--probes-in", self.write("prev-probes.tsv", builder.render_probes(cache, ROWS, "S")),
        )

    def test_a_first_run_probes_everything_and_writes_all_three_files(self):
        status, api = self.build(self.args())
        self.assertEqual(status, 0)
        self.assertEqual(len(api.urls), 3)
        self.assertIn("g\t19800422\t1980\tperiodical\t1\t", self.read("v2.txt"))
        self.assertTrue(self.read("v1.txt").startswith("berean-catalog\t1\tS\tm-1\t2026-10-05\n"))
        self.assertEqual(len(self.read("probes.tsv").strip().split("\n")), 4)

    def test_no_probe_makes_no_requests_and_keeps_the_cached_flags(self):
        probes = self.write("in.tsv", builder.render_probes({KM: ("0", TODAY)}, ROWS, "S"))
        status, api = self.build(self.args("--probes-in", probes, "--no-probe"))
        self.assertEqual(api.urls, [])
        self.assertIn("km\t198001\t1980\tperiodical\t0\t", self.read("v2.txt"))

    def test_a_flag_only_change_rewrites_v2_but_not_v1(self):
        old = TODAY - timedelta(days=100)
        status, _ = self.build(self.args(*self.published({G: ("1", old), KM: ("0", old), LFF: ("1", old)})))
        self.assertEqual(status, 0)
        self.assertIn("km\t198001\t1980\tperiodical\t1\t", self.read("v2.txt"))
        self.assertFalse(os.path.exists(self.path("v1.txt")), "older firmware must not see a new date for the same records")

    def test_an_unchanged_run_exits_3_and_still_writes_the_probe_cache(self):
        old = TODAY - timedelta(days=100)
        status, api = self.build(
            self.args(*self.published({G: ("1", old), KM: ("0", old), LFF: ("1", old)})), answers={"km": NOT_FOUND}
        )
        self.assertEqual(status, builder.EXIT_UNCHANGED)
        self.assertEqual(len(api.urls), 1)
        self.assertFalse(os.path.exists(self.path("v2.txt")))
        self.assertFalse(os.path.exists(self.path("v1.txt")))
        self.assertIn("km\t198001\t0\t2026-10-05", self.read("probes.tsv"), "the re-probe date must be kept")

    def test_seeding_resumes_probing_without_repeating_the_backlog(self):
        seed = self.write("prev-v2.txt", builder.render_index(ROWS, 2, "S", "m-1", "2026-09-28",
                                                              {G: ("1", TODAY), KM: ("0", TODAY)}))
        status, api = self.build(self.args("--seed-from-v2", seed))
        self.assertEqual(api.urls, [builder.probe_url("lff", "", "S")])

    def test_seeding_without_probing_keeps_every_published_flag(self):
        seed = self.write("prev-v2.txt", builder.render_index(ROWS, 2, "S", "m-1", "2026-09-28",
                                                              {G: ("1", TODAY), KM: ("0", TODAY)}))
        status, api = self.build(self.args("--seed-from-v2", seed, "--no-probe"))
        self.assertEqual(api.urls, [])
        self.assertIn("km\t198001\t1980\tperiodical\t0\t", self.read("v2.txt"))
        self.assertIn("km\t198001\t0\t2026-10-05", self.read("probes.tsv"))

    def test_a_throttle_writes_the_marker(self):
        marker = self.path("github_output")
        status, _ = self.build(self.args("--throttle-marker", marker), answers={"g": (429, b"")})
        with open(marker, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "throttled=1\n")


if __name__ == "__main__":
    unittest.main()
