from __future__ import annotations

import ctypes
from pathlib import Path
from typing import Optional, Sequence

import numpy as np


class Point(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
    ]


class Options(ctypes.Structure):
    _fields_ = [
        ("max_output_width", ctypes.c_int),
        ("return_warped_image", ctypes.c_int),
        ("min_confidence", ctypes.c_float),
    ]


class Result(ctypes.Structure):
    _fields_ = [
        ("success", ctypes.c_int),
        ("film_type", ctypes.c_int),
        ("confidence", ctypes.c_float),
        ("corners", Point * 4),
        ("inner_corners", Point * 4),
        ("corrected_width", ctypes.c_int),
        ("corrected_height", ctypes.c_int),
        ("inner_corrected_width", ctypes.c_int),
        ("inner_corrected_height", ctypes.c_int),
        ("outer_aspect", ctypes.c_float),
        ("inner_aspect", ctypes.c_float),
        ("error", ctypes.c_char * 128),
    ]


class InstantScan:
    def __init__(self, library_path: Optional[str | Path] = None):
        if library_path is None:
            library_path = Path(__file__).resolve().parents[1] / "build" / "libinstant_scan.so"

        self.lib = ctypes.CDLL(str(library_path))

        self.lib.instant_default_options.argtypes = []
        self.lib.instant_default_options.restype = Options

        self.lib.instant_scan_rgba.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            Options,
        ]
        self.lib.instant_scan_rgba.restype = Result

        self.lib.instant_classify_film_by_outer_ratio.argtypes = [
            ctypes.c_float,
            ctypes.c_float,
            ctypes.POINTER(ctypes.c_float),
        ]
        self.lib.instant_classify_film_by_outer_ratio.restype = ctypes.c_int

        self.lib.instant_extract_quad_rgba.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(Point),
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int,
        ]
        self.lib.instant_extract_quad_rgba.restype = ctypes.c_int

        self.lib.instant_extract_rgba.argtypes = self.lib.instant_extract_quad_rgba.argtypes
        self.lib.instant_extract_rgba.restype = ctypes.c_int

        self.lib.instant_extract_inner_rgba.argtypes = self.lib.instant_extract_quad_rgba.argtypes
        self.lib.instant_extract_inner_rgba.restype = ctypes.c_int

        self.lib.instant_film_type_name.argtypes = [ctypes.c_int]
        self.lib.instant_film_type_name.restype = ctypes.c_char_p

    def film_name(self, film_type: int) -> str:
        return self.lib.instant_film_type_name(film_type).decode("utf-8")

    def classify_by_outer_ratio(self, width: float, height: float) -> tuple[int, float, str]:
        confidence = ctypes.c_float(0.0)
        film_type = self.lib.instant_classify_film_by_outer_ratio(
            ctypes.c_float(width),
            ctypes.c_float(height),
            ctypes.byref(confidence),
        )
        return film_type, confidence.value, self.film_name(film_type)

    def scan_rgba(self, image: np.ndarray, options: Optional[Options] = None) -> Result:
        if image.dtype != np.uint8:
            raise TypeError("image must be uint8")
        if image.ndim != 3 or image.shape[2] != 4:
            raise ValueError("image must have shape HxWx4 RGBA")

        image = np.ascontiguousarray(image)
        height, width, _ = image.shape

        if options is None:
            options = self.lib.instant_default_options()

        ptr = image.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
        return self.lib.instant_scan_rgba(ptr, width, height, image.strides[0], options)

    def _extract_with_corners(
        self,
        image: np.ndarray,
        corners: Sequence[Point],
        output_width: int,
        output_height: int,
        fn_name: str = "instant_extract_quad_rgba",
    ) -> np.ndarray:
        if image.dtype != np.uint8:
            raise TypeError("image must be uint8")
        if image.ndim != 3 or image.shape[2] != 4:
            raise ValueError("image must have shape HxWx4 RGBA")
        if output_width <= 0 or output_height <= 0:
            raise ValueError("output dimensions must be positive")

        image = np.ascontiguousarray(image)
        height, width, _ = image.shape
        out = np.empty((output_height, output_width, 4), dtype=np.uint8)

        if isinstance(corners, ctypes.Array):
            point_array = corners
        else:
            point_array = (Point * 4)(*corners)

        fn = getattr(self.lib, fn_name)
        ok = fn(
            image.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
            width,
            height,
            image.strides[0],
            point_array,
            output_width,
            output_height,
            out.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
            out.strides[0],
        )
        if not ok:
            raise RuntimeError(f"{fn_name} failed")
        return out

    def extract_rgba(self, image: np.ndarray, result: Result, output_width: int, output_height: int) -> np.ndarray:
        """Perspective-correct the detected outer film frame, including the border."""
        return self._extract_with_corners(image, result.corners, output_width, output_height, "instant_extract_rgba")

    def extract_inner_rgba(self, image: np.ndarray, result: Result, output_width: int, output_height: int) -> np.ndarray:
        """Perspective-correct only the visible inner image area."""
        return self._extract_with_corners(image, result.inner_corners, output_width, output_height, "instant_extract_inner_rgba")

    def extract_quad_rgba(self, image: np.ndarray, corners: Sequence[Point], output_width: int, output_height: int) -> np.ndarray:
        """Generic helper for perspective-correcting an arbitrary quadrilateral."""
        return self._extract_with_corners(image, corners, output_width, output_height, "instant_extract_quad_rgba")
