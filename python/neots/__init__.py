"""
NeoTextSynthesizer - A high-performance OCR training data generator.

This package provides tools for generating synthetic OCR training images
with configurable text sampling, font rendering, and image processing.
"""

import json
import os
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Optional, Union

import numpy as np

from neots._core import NeoTextSynthesizer as _NeoTextSynthesizer
from neots.flatten_wikipedia_generator import FlattenWikipediaGenerator


def _load_bundled_config(name: str) -> str:
    """Load a bundled config file and return its content as a string."""
    ref = importlib_resources.files("neots") / "data" / "configs" / f"{name}.json"
    return ref.read_text(encoding="utf-8")


class NeoTextSynthesizer:
    """
    High-performance OCR training data generator.

    Wraps the C++ NeoTextSynthesizer with a Pythonic interface.

    Use the class methods to create instances:
        - ``NeoTextSynthesizer.from_config(config_dict)``
        - ``NeoTextSynthesizer.from_config_file(path)``
        - ``NeoTextSynthesizer.from_default(preset="preset")``
    """

    def __init__(self, config_str: str):
        """
        Internal constructor. Accepts a YAML/JSON configuration string.

        Users should prefer ``from_config``, ``from_config_file``, or
        ``from_default`` instead of calling this directly.
        """
        self._impl = _NeoTextSynthesizer(config_str)

    @classmethod
    def from_config(cls, config: dict) -> "NeoTextSynthesizer":
        """
        Create a NeoTextSynthesizer from a Python dictionary.

        Parameters
        ----------
        config : dict
            Configuration dictionary (same structure as config.json/yaml).

        Returns
        -------
        NeoTextSynthesizer
        """
        config_str = json.dumps(config, ensure_ascii=False)
        return cls(config_str)

    @classmethod
    def from_config_file(cls, path: str) -> "NeoTextSynthesizer":
        """
        Create a NeoTextSynthesizer from a JSON or YAML config file.

        Parameters
        ----------
        path : str
            Path to a configuration file (.json, .yaml, .yml).

        Returns
        -------
        NeoTextSynthesizer

        Raises
        ------
        FileNotFoundError
            If the config file does not exist.
        ValueError
            If the file content is invalid.
        """
        if not os.path.isfile(path):
            raise FileNotFoundError(f"Config file not found: {path}")
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        return cls(content)

    @classmethod
    def from_default(cls, preset: str = "preset") -> "NeoTextSynthesizer":
        """
        Create a NeoTextSynthesizer from a bundled default configuration.

        Parameters
        ----------
        preset : str
            Name of the preset: ``"preset"`` (full multi-language config)
            or ``"minimal"`` (empty text_sampler, no bg images).

        Returns
        -------
        NeoTextSynthesizer

        Raises
        ------
        ValueError
            If the preset name is not recognized.
        """
        valid_presets = ("preset", "minimal")
        if preset not in valid_presets:
            raise ValueError(
                f"Unknown preset '{preset}'. Must be one of: {valid_presets}"
            )
        content = _load_bundled_config(preset)
        return cls(content)

    def generate(self, total: int, workers: int = 0, show_progress: bool = True):
        """
        Generate a batch of OCR training images.

        This method blocks until all images are generated.

        Parameters
        ----------
        total : int
            Number of images to generate.
        workers : int, optional
            Number of worker threads. 0 means auto-detect (default: 0).
        show_progress : bool, optional
            Whether to display a progress bar (default: True).
        """
        self._impl.generate(total, workers, show_progress)

    def generate_instance(
        self,
        text: str,
        type: str = "PIL",
        path: Optional[str] = None,
    ):
        """
        Generate a single OCR image instance.

        Parameters
        ----------
        text : str
            The text to render.
        type : str
            Output type: "file", "PIL", or "numpy" (default: "PIL").
        path : str, optional
            Output file path (required when type="file").

        Returns
        -------
        PIL.Image.Image or numpy.ndarray or None
            - If type="PIL": returns a PIL Image in RGB mode.
            - If type="numpy": returns a numpy array of shape (H, W, 3) in RGB.
            - If type="file": returns None (image saved to path).
        """
        if type == "file":
            if path is None:
                raise ValueError("path is required when type='file'")
            self._impl.generate_instance_file(text, path)
            return None
        elif type == "PIL":
            try:
                from PIL import Image
            except ImportError:
                raise ImportError(
                    "Pillow is required for PIL output. Install it with: pip install Pillow"
                )
            arr = self._impl.generate_instance_explicit(text)
            return Image.fromarray(np.asarray(arr))
        elif type == "numpy":
            return np.asarray(self._impl.generate_instance_explicit(text))
        else:
            raise ValueError(f"Unknown type: {type}. Must be 'file', 'PIL', or 'numpy'")

    @property
    def config(self) -> dict:
        """Get the active configuration as a dictionary."""
        return json.loads(self._impl.get_config_json())

    @staticmethod
    def get_default_config() -> dict:
        """Get the default preset configuration as a dictionary."""
        return json.loads(_load_bundled_config("preset"))

    @staticmethod
    def get_minimal_config() -> dict:
        """Get the minimal configuration as a dictionary."""
        return json.loads(_load_bundled_config("minimal"))

    @staticmethod
    def output_default_dict(path: str, type: str = "yaml"):
        """
        Output the default preset configuration to a file.

        Parameters
        ----------
        path : str
            Output file path.
        type : str
            Format: "yaml" or "json" (default: "yaml").
        """
        config = json.loads(_load_bundled_config("preset"))
        _output_config(config, path, type)

    @staticmethod
    def output_minimal_dict(path: str, type: str = "yaml"):
        """
        Output the minimal configuration to a file.

        Parameters
        ----------
        path : str
            Output file path.
        type : str
            Format: "yaml" or "json" (default: "yaml").
        """
        config = json.loads(_load_bundled_config("minimal"))
        _output_config(config, path, type)


def _output_config(config: dict, path: str, fmt: str = "yaml"):
    """Write a config dictionary to a file in the specified format."""
    if fmt in ("yaml", "yml"):
        try:
            import yaml
        except ImportError:
            raise ImportError(
                "PyYAML is required for YAML output. Install it with: pip install pyyaml"
            )
        with open(path, "w", encoding="utf-8") as f:
            yaml.dump(config, f, default_flow_style=False, allow_unicode=True, sort_keys=False)
    elif fmt == "json":
        with open(path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=4, ensure_ascii=False)
            f.write("\n")
    else:
        raise ValueError(f"Unsupported config format: {fmt}. Must be 'yaml' or 'json'.")


__all__ = [
    "NeoTextSynthesizer",
    "FlattenWikipediaGenerator",
]