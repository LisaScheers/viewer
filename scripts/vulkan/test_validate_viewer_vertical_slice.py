"""Negative evidence tests for the real viewer smoke's acceptance gate."""

from pathlib import Path
import tempfile
import unittest

from validate_viewer_vertical_slice import SmokeFailure, validate_viewer_log, progress_landmarks, validate_progress_change


class ViewerEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="viewer-vulkan-evidence-")
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "SecondLife.log"
        self.records = [
            "window_owner=viewer callbacks=viewer live_windows=1",
            "status=suspended frames=100 resumed_frames=0",
            "status=resumed frames=101 resumed_frames=1",
            "status=stopped frames=130 rebuilds=5 suspends=1 resumed_frames=30 "
            "validation_messages=0 gl_context=0 gl_manager=0 gl_context_create_attempts=0 "
            "gl_swap_attempts=0 gl_audit_armed=1 live_windows=1",
            "window_retired=1 live_windows=0 gl_context_create_attempts=0 gl_swap_attempts=0 "
            "gl_audit_armed=1 gl_manager=0 shader_manager=0",
        ]

    def validate(self):
        self.path.write_text("\n".join("#VulkanViewerSlice# " + record for record in self.records))
        return validate_viewer_log(self.path)

    def test_presented_restore_passes(self):
        self.assertEqual(self.validate()["resumed_frames"], 30)

    def test_pre_minimize_frames_cannot_prove_restore(self):
        self.records[3] = self.records[3].replace("resumed_frames=30", "resumed_frames=0")
        with self.assertRaisesRegex(SmokeFailure, "post-restore"):
            self.validate()

    def test_missing_resume_record_fails(self):
        del self.records[2]
        with self.assertRaisesRegex(SmokeFailure, "observed suspension"):
            self.validate()

    def test_resume_before_suspend_fails(self):
        self.records[1], self.records[2] = self.records[2], self.records[1]
        with self.assertRaisesRegex(SmokeFailure, "out of order"):
            self.validate()

    def test_restore_without_successful_frame_fails(self):
        self.records[2] = self.records[2].replace("frames=101", "frames=100")
        with self.assertRaisesRegex(SmokeFailure, "first successfully presented"):
            self.validate()

    def test_inconsistent_final_progress_fails(self):
        self.records[3] = self.records[3].replace("frames=130", "frames=129")
        with self.assertRaisesRegex(SmokeFailure, "does not match"):
            self.validate()

    def test_validation_or_opengl_activity_fails(self):
        original = self.records[3]
        for field in ("validation_messages", "gl_context", "gl_manager", "gl_context_create_attempts", "gl_swap_attempts"):
            with self.subTest(field=field):
                self.records[3] = original.replace(f"{field}=0", f"{field}=1")
                with self.assertRaises(SmokeFailure):
                    self.validate()

    def test_window_retired_before_renderer_fails(self):
        self.records[3], self.records[4] = self.records[4], self.records[3]
        with self.assertRaisesRegex(SmokeFailure, "before Vulkan runtime shutdown"):
            self.validate()


class ProgressPixelTests(unittest.TestCase):
    width, height = 640, 240

    def pixels(self, bar_width=100, text=True):
        pixels = bytearray(self.width * self.height * 3)
        def rectangle(left, top, width, height, rgb):
            for y in range(top, top + height):
                for x in range(left, left + width):
                    offset = (y * self.width + x) * 3
                    pixels[offset:offset + 3] = bytes(rgb)
        rectangle(100, 120, bar_width, 12, (0, 100, 180))
        if text:
            for column in range(10):
                rectangle(100 + column * 8, 60, 3, 12, (240, 240, 240))
        return bytes(pixels)

    def test_expected_landmarks(self):
        self.assertEqual(progress_landmarks(self.pixels(), self.width, self.height), (100, 120, 200, 132))

    def test_blank_and_gradient_are_not_ui(self):
        for pixel in (bytes((0, 0, 0)), bytes((0, 100, 180))):
            with self.subTest(pixel=pixel), self.assertRaises(SmokeFailure):
                progress_landmarks(pixel * (self.width * self.height), self.width, self.height)

    def test_geometry_without_text_is_rejected(self):
        with self.assertRaisesRegex(SmokeFailure, "text landmarks"):
            progress_landmarks(self.pixels(text=False), self.width, self.height)

    def test_threefold_fill_change(self):
        validate_progress_change(self.pixels(100), self.pixels(300), self.width, self.height)

    def test_unchanged_fill_is_rejected(self):
        with self.assertRaisesRegex(SmokeFailure, "25 to 75"):
            validate_progress_change(self.pixels(), self.pixels(), self.width, self.height)

    def test_unrelated_repaint_is_rejected(self):
        changed = bytearray(self.pixels(300))
        changed[:self.width * 10 * 3] = bytes((255, 255, 255)) * (self.width * 10)
        with self.assertRaisesRegex(SmokeFailure, "unrelated"):
            validate_progress_change(self.pixels(), bytes(changed), self.width, self.height)


if __name__ == "__main__":
    unittest.main()
