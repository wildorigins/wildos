"""Setup's automatic recovery: each stage checks what it needs, a stage that
fails is tried again, a second failure clears what it made and goes back a
stage, and two more failures stop with the problem shown as before."""

import contextlib
import os
import shutil
import tempfile
import threading
import time
import unittest

from winmode import setup


@contextlib.contextmanager
def online_as(answers):
    """setup.online() answering from a list (the last answer repeats), with
    the offline wait looking again at once."""
    answers = list(answers)
    saved = setup.online, setup.OFFLINE_POLL

    def online():
        return answers.pop(0) if len(answers) > 1 else answers[0]
    setup.online, setup.OFFLINE_POLL = online, 0.01
    try:
        yield
    finally:
        setup.online, setup.OFFLINE_POLL = saved


class Svc:
    def __init__(self, rec=None):
        self.cfg = {"domain": "wildos-windows", "account": "wild", "share_root": "/nonexistent-share"}
        self.state = {"setup": rec or {"running": True, "options": {"office": False}, "licence": {"source": "none"},
                                       "stages": {}, "done": []}}

    def save_state(self):
        pass

    def notify(self):
        pass

    def new_guest_password(self):
        return "Pw" * 12 + "a7Q"


class Moves(unittest.TestCase):
    def test_retry_then_back_then_stop(self):
        move, r = setup.next_move(None, "install")
        self.assertEqual(move, "retry")
        move, r = setup.next_move(r, "install")
        self.assertEqual(move, "back")
        self.assertEqual(r["back"], "media")
        # Going back: one more try, then stop.
        move, r = setup.next_move(r, "media")
        self.assertEqual(move, "retry")
        move, r = setup.next_move(r, "install")
        self.assertEqual(move, "stop")

    def test_failures_must_be_in_a_row(self):
        _, r = setup.next_move(None, "activate")
        # A different stage failing next starts its own count.
        move, r = setup.next_move(r, "tune")
        self.assertEqual(move, "retry")
        self.assertEqual(r, {"stage": "tune", "fails": 1})

    def test_first_stage_goes_back_to_itself(self):
        _, r = setup.next_move(None, "media")
        move, r = setup.next_move(r, "media")
        self.assertEqual((move, r["back"]), ("back", "media"))

    def test_success_ends_recovery(self):
        _, r = setup.next_move(None, "tune")
        self.assertIsNone(setup.recovered(r, "tune"))
        _, r = setup.next_move(r, "tune")
        # Going back to activate: activate finishing is not the end of it,
        # tune finishing is.
        self.assertEqual(setup.recovered(r, "activate"), r)
        self.assertIsNone(setup.recovered(r, "tune"))


class Flow(unittest.TestCase):
    """work() with the stages replaced by scripted ones."""

    def setUp(self):
        self.saved_wait = setup.RETRY_WAIT
        setup.RETRY_WAIT = 0
        self.s = setup.Setup(Svc())
        self.ran, self.purged = [], []
        self.fail = {}
        self.s.purge = lambda stage: self.purged.append(stage)
        for stage in setup.STAGES:
            setattr(self.s, "step_" + stage, self.stage(stage))
        self.s.finish = lambda: self.s.save(running=False, stage="done")

    def tearDown(self):
        setup.RETRY_WAIT = self.saved_wait

    def stage(self, name):
        def run():
            flags = setup.Setup.STAGE_FLAGS[name]
            if all(self.s.done(f) for f in flags):
                return
            self.s.set_stage(name, "active")
            self.ran.append(name)
            if self.fail.get(name):
                self.fail[name] -= 1
                raise RuntimeError(f"{name} broke")
            for f in flags:
                self.s.mark(f)
            self.s.stage_done(name)
        return run

    def test_one_failure_is_tried_again(self):
        self.fail = {"activate": 1}
        self.s.work()
        self.assertEqual(self.ran, ["media", "install", "activate", "activate", "tune"])
        self.assertEqual(self.purged, [])
        rec = self.s.rec
        self.assertEqual(rec["stage"], "done")
        self.assertIsNone(rec.get("failed"))
        self.assertIsNone(rec.get("recovery"))
        self.assertIsNone(rec.get("notice"))

    def test_two_failures_purge_and_go_back(self):
        self.fail = {"install": 2}
        notices = []
        save = self.s.save

        def watch(**changes):
            save(**changes)
            if changes.get("notice"):
                notices.append(changes["notice"]["text"])
        self.s.save = watch
        self.s.work()
        self.assertEqual(self.ran, ["media", "install", "install", "media", "install", "activate", "tune"])
        # The retry starts the install from the installer (nothing copied
        # yet); going back clears it again.
        self.assertEqual(self.purged, ["install", "install"])
        self.assertEqual(notices, [setup.RETRY_NOTICE, setup.BACK_NOTICE])
        self.assertEqual(self.s.rec["stage"], "done")
        self.assertIsNone(self.s.rec.get("recovery"))

    def test_install_retry_keeps_a_copied_windows(self):
        self.s.save(copied_at=1)
        self.fail = {"install": 1}
        self.s.work()
        self.assertEqual(self.purged, [])

    def test_going_back_failing_twice_shows_the_problem(self):
        self.fail = {"tune": 4}
        self.s.work()
        # tune, tune again, back to activate and tune, tune once more: stop.
        self.assertEqual(self.ran, ["media", "install", "activate", "tune", "tune", "activate", "tune", "tune"])
        self.assertEqual(self.purged, ["tune"])
        rec = self.s.rec
        self.assertTrue(rec["running"])
        self.assertEqual(rec["problem"]["kind"], "failed")
        self.assertIn("tune broke", rec["problem"]["detail"])
        self.assertTrue(rec["failed"])
        self.assertEqual(rec["stages"]["tune"]["state"], "failed")
        self.assertIsNone(rec.get("notice"))

    def test_try_again_starts_the_tries_afresh(self):
        self.fail = {"tune": 4}
        self.s.work()
        self.s.launch = lambda: None
        self.assertEqual(self.s.start({}, 1000), "")
        self.assertIsNone(self.s.rec.get("recovery"))
        self.assertIsNone(self.s.rec.get("failed"))

    def test_problem_only_the_person_can_fix_stops_at_once(self):
        def media():
            self.ran.append("media")
            raise setup.Problem("failed", "This PC's licence couldn't be read.", retry=False)
        self.s.step_media = media
        self.s.work()
        self.assertEqual(self.ran, ["media"])
        self.assertEqual(self.s.rec["failed"], "This PC's licence couldn't be read.")

    def test_downloads_still_pause(self):
        def media():
            raise setup.Problem("download", "Microsoft didn't allow the download from here.")
        self.s.step_media = media
        with online_as([True]):
            self.s.work()
        self.assertTrue(self.s.rec["paused"])
        self.assertEqual(self.s.rec["problem"], {"kind": "download", "text": "Microsoft didn't allow the download from here."})
        self.assertIsNone(self.s.rec.get("recovery"))

    def test_offline_waits_and_carries_on_by_itself(self):
        tries = {"media": 1}
        problems = []
        save = self.s.save

        def watch(**changes):
            save(**changes)
            if "problem" in changes:
                problems.append(changes["problem"])
        self.s.save = watch

        def media():
            self.ran.append("media")
            if tries["media"]:
                tries["media"] -= 1
                raise setup.Problem("download", "Windows mode couldn't download the parts it runs on.")
            self.s.stage_done("media")
        self.s.step_media = media
        # Offline when the step fails and for two looks after, then back.
        with online_as([False, False, False, True]):
            self.s.work()
        self.assertEqual(self.ran[:2], ["media", "media"])
        self.assertEqual(self.s.rec["stage"], "done")
        self.assertFalse(self.s.rec.get("paused"))
        self.assertIsNone(self.s.rec.get("problem"))
        offline = [p for p in problems if p and p.get("offline")]
        self.assertEqual(len(offline), 1)
        self.assertEqual(offline[0]["text"], setup.OFFLINE_TEXT)

    def test_start_over_while_offline_stops_the_wait(self):
        def media():
            raise setup.Problem("download", "The download stopped.")
        self.s.step_media = media
        self.s.svc.refresh = lambda: None
        with online_as([False] * 1000):
            worker = threading.Thread(target=self.s.work)
            self.s.thread = worker
            worker.start()
            deadline = time.monotonic() + 5
            while not (self.s.rec.get("problem") or {}).get("offline") and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(self.s.rec["problem"]["offline"])
            self.s.stop.set()
            self.s.resume_event.set()
            worker.join(5)
        self.assertFalse(worker.is_alive())

    def test_nothing_is_written_once_start_over_cleared_the_record(self):
        self.s.stop.set()
        self.s.svc.state.pop("setup")
        self.s.save(download={"received": 1})
        self.s.set_stage("media", "active", 50)
        self.assertNotIn("setup", self.s.svc.state)

    def test_missing_result_of_an_earlier_stage_is_redone(self):
        gone = {"once": True}
        tune = self.s.step_tune

        def step_tune():
            if gone["once"] and self.s.done("install"):
                gone["once"] = False
                raise setup.Redo("install", "the virtual PC's disk is missing")
            tune()
        self.s.step_tune = step_tune
        self.s.work()
        self.assertEqual(self.ran, ["media", "install", "activate", "install", "activate", "tune"])
        self.assertEqual(self.purged, ["install"])
        # Not a failure: no tries used.
        self.assertIsNone(self.s.rec.get("recovery"))

    def test_stop_ends_the_worker(self):
        self.s.stop.set()
        self.fail = {"install": 1}
        self.s.work()
        self.assertEqual(self.ran, ["media", "install"])


class Files(unittest.TestCase):
    """prepare_start, define and purge on a temporary state folder."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.saved = {k: getattr(setup, k) for k in ("CACHE", "SECRETS", "DISK", "WINDOWS_ISO", "VIRTIO", "PAYLOAD",
                                                     "TEMPLATES", "_remove")}
        self.saved_virsh = (setup.virsh.state, setup.virsh.run, setup.virsh.defined_memory)
        self.saved_state_dir = setup.config.STATE_DIR
        cache = os.path.join(self.tmp, "cache")
        os.makedirs(cache)
        setup.config.STATE_DIR = self.tmp
        setup.CACHE, setup.SECRETS = cache, os.path.join(self.tmp, "secrets")
        setup.DISK = os.path.join(self.tmp, "windows.qcow2")
        setup.WINDOWS_ISO = os.path.join(cache, "windows.iso")
        setup.VIRTIO = os.path.join(cache, "virtio-win.iso")
        setup.PAYLOAD = os.path.join(cache, "payload.iso")
        setup.TEMPLATES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "templates")
        self.domain = {"state": "shut off"}
        self.calls = []
        setup.virsh.state = lambda d: self.domain["state"]
        setup.virsh.defined_memory = lambda d: 4096

        def run(*args, **kw):
            self.calls.append(args)
            if args[0] == "define":
                with open(args[1]) as f:
                    self.defined = f.read()
            if args[0] == "undefine":
                self.domain["state"] = ""
            return "00000000-0000-0000-0000-000000000000" if args[0] == "domuuid" else ""
        setup.virsh.run = run
        self.s = setup.Setup(Svc())
        self.made = []
        self.s.network = lambda: self.made.append("network")
        self.s.step_media = lambda: self.made.append("media")
        self.s.prepare_licence = lambda: self.made.append("licence")

        def payload():
            self.made.append("payload")
            self.touch(setup.PAYLOAD)
        self.s.payload = payload

    def tearDown(self):
        for k, v in self.saved.items():
            setattr(setup, k, v)
        setup.virsh.state, setup.virsh.run, setup.virsh.defined_memory = self.saved_virsh
        setup.config.STATE_DIR = self.saved_state_dir
        shutil.rmtree(self.tmp, ignore_errors=True)

    @staticmethod
    def touch(path, iso=False):
        with open(path, "wb") as f:
            if iso:
                f.truncate(60 * 1000 ** 2)
                f.seek(0x8001)
                f.write(b"CD001")

    def downloads(self):
        self.touch(setup.WINDOWS_ISO, iso=True)
        self.touch(setup.VIRTIO, iso=True)

    def test_missing_payload_is_made_again_before_starting(self):
        # Alex's case: the virtual PC was defined, its payload had gone, and
        # every try failed at once in virsh start.
        self.downloads()
        self.touch(setup.DISK)
        self.s.save(install_started=1)
        self.s.define = lambda: self.made.append("define")
        st = self.s.prepare_start("wildos-windows")
        self.assertEqual(st, "shut off")
        self.assertEqual(self.made, ["licence", "payload", "network", "define"])
        self.assertTrue(os.path.exists(setup.PAYLOAD))

    def test_missing_download_goes_back_to_media(self):
        self.touch(setup.VIRTIO, iso=True)
        self.touch(setup.PAYLOAD)
        self.s.define = lambda: self.made.append("define")
        self.s.prepare_start("wildos-windows")
        self.assertEqual(self.made[0], "media")

    def test_running_virtual_pc_is_left_alone(self):
        self.domain["state"] = "running"
        self.assertEqual(self.s.prepare_start("wildos-windows"), "running")
        self.assertEqual(self.made, [])

    def test_missing_disk_after_install_redoes_install(self):
        self.s.mark("install")
        with self.assertRaises(setup.Redo) as caught:
            self.s.prepare_start("wildos-windows")
        self.assertEqual(caught.exception.stage, "install")

    @unittest.skipUnless(os.path.exists("/proc/cpuinfo"), "Linux only")
    def test_disc_no_longer_needed_is_left_out(self):
        # Windows has copied itself to the disk and its installer has gone:
        # the drive is defined empty instead of fetching 6 GB again.
        self.touch(setup.VIRTIO, iso=True)
        self.touch(setup.PAYLOAD)
        self.touch(setup.DISK)
        self.s.save(install_started=1, copied_at=2)
        self.s.cfg.update(network="wildos-win", share_root=os.path.join(self.tmp, "share"))
        self.s.prepare_start("wildos-windows")
        self.assertNotIn("media", self.made)
        self.assertNotIn(setup.WINDOWS_ISO, self.defined)
        self.assertIn(setup.VIRTIO, self.defined)
        self.assertIn(setup.PAYLOAD, self.defined)
        self.assertIn("<target dev='sda' bus='sata'/>", self.defined)

    def test_purge_install_only_touches_its_own_files(self):
        share = os.path.join(self.tmp, "share", "Documents")
        os.makedirs(share)
        self.touch(os.path.join(share, "letter.docx"))
        self.downloads()
        for path in (setup.DISK, setup.PAYLOAD, os.path.join(self.tmp, "domain.xml"), os.path.join(self.tmp, "network.xml")):
            self.touch(path)
        self.s.save(install_started=5, booted_at=5, boot_tries=2, copied_at=9)
        self.s.purge("install")
        for gone in (setup.DISK, setup.PAYLOAD, os.path.join(self.tmp, "domain.xml")):
            self.assertFalse(os.path.exists(gone), gone)
        for kept in (setup.WINDOWS_ISO, setup.VIRTIO, os.path.join(self.tmp, "network.xml"),
                     os.path.join(share, "letter.docx")):
            self.assertTrue(os.path.exists(kept), kept)
        self.assertIn(("undefine", "wildos-windows", "--nvram", "--managed-save"), self.calls)
        self.assertFalse(self.s.rec.get("install_started"))
        self.assertFalse(self.s.rec.get("copied_at"))

    def test_purge_media_keeps_a_finished_download_and_the_typed_key(self):
        self.downloads()
        os.makedirs(setup.SECRETS)
        for name in ("key", "msdm.bin", setup.ACTIVATION_KEY):
            self.touch(os.path.join(setup.SECRETS, name))
        self.touch(setup.WINDOWS_ISO + ".part")
        self.s.purge("media")
        self.assertTrue(os.path.exists(setup.WINDOWS_ISO))
        self.assertTrue(os.path.exists(setup.VIRTIO))
        self.assertFalse(os.path.exists(setup.WINDOWS_ISO + ".part"))
        self.assertFalse(os.path.exists(os.path.join(setup.SECRETS, "key")))
        self.assertTrue(os.path.exists(os.path.join(setup.SECRETS, setup.ACTIVATION_KEY)))

    def test_install_cut_short_starts_again_from_the_installer(self):
        class Started(Exception):
            pass
        purged = []
        self.s.purge = lambda stage: (purged.append(stage), self.s.save(install_started=None))

        def prepare(domain):
            raise Started()
        self.s.prepare_start = prepare
        self.touch(setup.DISK)
        self.s.save(install_started=1, booted_at=1)
        with self.assertRaises(Started):
            self.s.step_install()
        self.assertEqual(purged, ["install"])
        # Once Windows has copied itself, a restart carries on from the disk.
        purged.clear()
        self.s.save(install_started=1, copied_at=2)
        with self.assertRaises(Started):
            self.s.step_install()
        self.assertEqual(purged, [])

    def test_needs(self):
        self.assertEqual(self.s.needs(), {setup.WINDOWS_ISO: True, setup.VIRTIO: True, setup.PAYLOAD: True})
        self.s.save(copied_at=1)
        self.assertFalse(self.s.needs()[setup.WINDOWS_ISO])
        self.s.mark("tune")
        self.assertFalse(self.s.needs()[setup.VIRTIO])


if __name__ == "__main__":
    unittest.main()
