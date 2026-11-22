"""The Kraken protocol against a pretend cooler that, like the real one, sends
status reports of its own between replies and is slow to answer.

  python3 tests/kraken_test.py
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from wildos_cooler import kraken  # noqa: E402


def status_report(liquid=34.5):
    msg = [0] * 64
    msg[0], msg[1] = 0x75, 0x02
    msg[15], msg[16] = int(liquid), round(liquid * 10) % 10
    return msg


class FakeHid:
    def __init__(self, cooler):
        self.cooler = cooler

    def clear_enqueued_reports(self):
        # The real queue is emptied, but a reply still on its way survives.
        self.cooler.queue = [m for m in self.cooler.queue if m is self.cooler.late]


class FakeDev:
    """Answers each command one report late, after a status report."""

    def __init__(self):
        self.queue = []
        self.late = None
        self.device = FakeHid(self)
        self.bulk_device = self
        self.commands = []
        self.stored = False

    def _write(self, data):
        command = list(data)
        self.commands.append(command[:3])
        reply = [0] * 64
        reply[0], reply[1] = command[0] + 1, command[1]
        reply[14] = 0x01
        if command[:2] == [0x74, 0x01]:
            reply[15], reply[16] = 36, 2
            reply[17], reply[18] = 0x9A, 0x07
            reply[19] = 60
        if command[:2] == [0x36, 0x02]:
            self.stored = True
        self.queue.append(status_report())
        self.queue.append(reply)

    def _read(self):
        return self.queue.pop(0) if self.queue else status_report()

    def write(self, endpoint, data):
        pass


def cooler_with(dev):
    cooler = kraken.Kraken.__new__(kraken.Kraken)
    cooler._dev = dev
    cooler.size = 4
    cooler._next_slot = 0
    cooler._ring_ready = False
    cooler._frame_bytes = 4 * 4 * 4
    cooler._slot_units = 1
    return cooler


class KrakenProtocol(unittest.TestCase):
    def test_readings_skip_status_reports_and_stale_replies(self):
        dev = FakeDev()
        cooler = cooler_with(dev)
        # A reply nobody read, as the old frame code left behind.
        stale = [0] * 64
        stale[0], stale[1] = 0x37, 0x02
        dev.late = stale
        dev.queue.append(stale)
        got = cooler.readings()
        self.assertEqual(got.liquid, 36.2)
        self.assertEqual(got.pump_rpm, 0x079A)
        self.assertEqual(got.pump_duty, 60)

    def test_frames_keep_readings_right(self):
        dev = FakeDev()
        cooler = cooler_with(dev)
        cooler.start_frames = lambda: setattr(cooler, "_ring_ready", True)
        for _ in range(20):
            self.assertTrue(cooler.send_frame(bytes(64)))
            self.assertEqual(cooler.readings().liquid, 36.2)

    def test_picture_shown_only_after_it_is_stored(self):
        dev = FakeDev()
        cooler = cooler_with(dev)
        cooler._ring_ready = True
        cooler.send_frame(bytes(64))
        stored_at = next(i for i, c in enumerate(dev.commands) if c[:2] == [0x36, 0x02])
        shown_at = next(i for i, c in enumerate(dev.commands) if c[:2] == [0x38, 0x01])
        self.assertLess(stored_at, shown_at)

    def test_liquid_screen_empties_every_slot(self):
        dev = FakeDev()
        cooler = cooler_with(dev)
        cooler._ring_ready = True
        cooler.show_liquid()
        deleted = sorted(c[2] for c in dev.commands if c[:2] == [0x32, 0x02])
        self.assertEqual(deleted, list(range(kraken.SLOTS)))
        self.assertFalse(cooler._ring_ready)

    def test_silent_cooler_is_an_error_not_a_hang(self):
        dev = FakeDev()
        dev._write = lambda data: None
        cooler = cooler_with(dev)
        with self.assertRaises(kraken.CoolerError):
            cooler.readings()


if __name__ == "__main__":
    unittest.main()
