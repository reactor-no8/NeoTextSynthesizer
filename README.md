# NeoTextSynthesizer (neots)

A high-performance, multi-threaded synthetic OCR training data generator built with C++.

NeoTextSynthesizer generates realistic text images with diverse fonts, backgrounds, and visual effects — ideal for training Text Recognition models.

## Features

- **High Performance**: Multi-threaded C++ core with shared glyph cache, background image cache, and lock-free rendering pipeline
- **Multi-language Support**: Built-in support for Chinese (Simplified & Traditional), Japanese, Korean, English, German, French, Italian, Spanish, Russian, and extensible to any language
- **Flexible Text Sampling**: Sequential corpus streaming and random character sampling with nested probability-weighted groups
- **Rich Visual Effects**: Random fonts, font sizes, scale variations, italic/underline/strikethrough effects, diverse backgrounds (solid colors, gradients, real images)
- **Python Integration**: Full Python API via nanobind with NumPy/PIL support
- **CLI Tools**: `neots` for workspace initialization and batch generation, `flat-wiki` for Wikipedia corpus generation
- **Cross-platform**: Supports Linux x86_64, Linux AArch64, macOS ARM64, and Windows x86_64

## Installation

Install pre-built wheels from PyPI:

```bash
pip install neots
```

## Quick Start

### CLI Usage

```bash
# Initialize a workspace
neots init

# Generate 10,000 images
neots generate --total 10000

# Generate with custom config and workers
neots generate --config config.yaml --total 50000 --workers 8

# Show help
neots help
```

### Wikipedia Corpus Generation (CLI)

```bash
# Generate a Chinese corpus from Wikipedia
flat-wiki --lang zh --output ./corpuses/zh.txt --min_len 5 --sample_count 10000

# Generate with vocabulary filter
flat-wiki -l en -o ./corpuses/en.txt -v dictionary.txt -n 5000
```

### Python API

```python
from neots import NeoTextSynthesizer

# Initialize from config file
synth = NeoTextSynthesizer.from_config_file("config.yaml")

# Initialize from a dictionary
config = NeoTextSynthesizer.get_default_config()
config["generate"]["output_height"] = 64
synth = NeoTextSynthesizer.from_config(config)

# Initialize from built-in default preset
synth = NeoTextSynthesizer.from_default()          # full preset
synth = NeoTextSynthesizer.from_default("minimal")  # minimal preset

# Batch generate (blocks until complete)
synth.generate(total=10000, workers=8, show_progress=True)

# Generate single image as PIL Image
img = synth.generate_instance("Hello World", type="PIL")
img.save("sample.png")

# Generate as NumPy array (H, W, 3) RGB
arr = synth.generate_instance("测试文本", type="numpy")

# Save directly to file
synth.generate_instance("サンプル", type="file", path="output.png")

# Export default config
NeoTextSynthesizer.output_default_dict("my_config.yaml", type="yaml")
NeoTextSynthesizer.output_default_dict("my_config.json", type="json")
```

### Corpus Generation (Python API)

```python
from neots import FlattenWikipediaGenerator

gen = FlattenWikipediaGenerator(
    lang="zh",                        # Language code
    output="./corpuses/zh.txt",       # Output file path
    vocab="dictionary_extended.txt",  # Optional: filter by vocabulary
    min_len=5,                        # Minimum line length
    sample_count=10000,               # Number of articles to process
)
gen.generate()
```

## Configuration

Configuration is structured into four sections. See config.yaml for a complete example after initialization.

### `text_sampler`

Controls the basic text sampling parameters and font selection strategy.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `min_targets` | 5 | Minimum characters per image |
| `max_targets` | 50 | Maximum characters per image |
| `font_size` | 55 | Base font size in pixels |
| `sample_strategy` | `"font-first"` | Strategy for handling missing characters: `"font-first"`, `"sample-first"`, or `"auto-fallback"` |
| `font_list` | `["./fonts"]` | List of font files or directories containing fonts |

#### Sample Strategies
- **`font-first`**: Selects a font first, then samples text. Any characters not supported by the chosen font are discarded. (Fastest)
- **`sample-first`**: Samples text first, then finds a font that supports all characters. If none exists, chooses the font with the highest coverage and discards unsupported characters.
- **`auto-fallback`**: Selects a primary font and samples text. For unsupported characters, it attempts to find a fallback font to render them. (Slowest but most comprehensive)

### `random_config`

Defines how text strings are sampled for rendering. Each entry has a probability weight and a data source.

| Type | Description |
|------|-------------|
| `sequential` | Streams text from a flat file. Ideal for natural language corpuses. |
| `random` | Randomly samples from character files or nested sub-groups. |

```yaml
random_config:
  # Stream Chinese text from a corpus file
  - type: sequential
    prob: 0.20
    from_file: ./corpuses/zh.txt

  # Random character sampling with nested groups
  - type: random
    prob: 0.20
    sub_items:
      - prob: 0.4
        from_file: ./symbols_characters/characters.txt
        section: [~, 3500]    # First 3500 lines only
      - prob: 0.1
        from_string: " "      # Inline source
```

Key parameters:
- `prob`: Probability weight (auto-normalized)
- `from_file` / `from_string`: Data source
- `section: [start, end]`: Slice of lines (`~` = null = beginning/end)
- `sub_items`: Nested probability groups (recursive)
- `traditional_prob`: Probability of simplified → traditional Chinese conversion

### `bg_sampler`

Controls background selection:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `bg_image_prob` | 0.3 | Probability of using a real background image |
| `gray_bg_prob` | 0.7 | Probability of gray vs. colored background |
| `bg_list` | `["./backgrounds"]` | List of background image files or directories |

### `post_process`

Controls visual appearance:

#### `text_effects`
| Parameter | Default | Description |
|-----------|---------|-------------|
| `effect_prob` | 0.2 | Probability of text effects (italic/underline/strikethrough) |
| `partial_effect_prob` | 0.8 | Probability that effects apply to only part of the text |

#### `text_paste`
| Parameter | Default | Description |
|-----------|---------|-------------|
| `scale_range` | [0.7, 1.1] | Random scale factor range |
| `recompute_width` | true | Recompute width after scaling |
| `margin_range` | [-5, 20] | Horizontal margin range (pixels) |
| `offset_prob` | 0.5 | Probability of random position offset |
| `h_offset_range` | [-7, 7] | Horizontal offset range |
| `v_offset_range` | [-5, 5] | Vertical offset range |

### `generate`

Controls output structure:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `output_height` | 48 | Output image height (width auto-calculated) |
| `out_dir` | ./generated | Output image directory |
| `out_jsonl` | ./generated.jsonl | Annotation file path |
| `batchsize` | 10000 | JSONL write batch size |
| `hierarchical_structure` | [100, 625] | Directory nesting levels |

## Font Setup

By default, neots uses its built-in default font `NotoSerifCJK-Regular.ttc` in `fonts/`. Place diverse `.ttf` / `.otf` / `.ttc` font files in the `fonts/` directory. The system will automatically index them and use them according to your chosen `sample_strategy`. **More fonts = better model generalization.** Aim for 50-200+ fonts covering your target scripts.

## Background Images

For realistic training data, add images to `backgrounds/` and set `bg_image_prob > 0`.

**Supported formats:** `.jpg`, `.jpeg`, `.png`, `.bmp`, `.webp`

## Output Format

Generated images are saved as PNG files in a hierarchical directory structure:

```
generated/
├── 00000001/
│   ├── 00000001/
│   │   ├── 00000001.png
│   │   ├── 00000002.png
│   │   └── ...
│   └── ...
└── ...
```

Annotations are saved in JSONL format (`generated.jsonl`):

```json
{"image":"00000001/00000001/00000001.png","text":"示例文本","length":248}
{"image":"00000001/00000001/00000002.png","text":"Sample text","length":315}
```

Each line contains:
- `image`: Relative path to the image file
- `text`: The rendered text content
- `length`: Image width in pixels

## Development

This project uses [vcpkg](https://github.com/microsoft/vcpkg) to manage native C++ dependencies, and uses [scikit-build-core](https://scikit-build-core.readthedocs.io/) + [nanobind](https://nanobind.readthedocs.io/) for the Python/C++ bridge. Development is now based on configuring the environment with vcpkg and building from source.

### Supported Platforms

- Linux x86_64
- Linux AArch64
- macOS ARM64
- Windows x86_64

### Build from Source with vcpkg

#### Windows

```powershell
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
$env:CMAKE_TOOLCHAIN_FILE="$PWD\vcpkg\scripts\buildsystems\vcpkg.cmake"
pip install -e . --no-build-isolation
```

#### macOS

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export CMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
pip install -e . --no-build-isolation
```

#### Linux

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export CMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
pip install -e . --no-build-isolation
```

If you prefer to build the native extension directly with CMake, make sure `CMAKE_TOOLCHAIN_FILE` points to the vcpkg toolchain before configuring the project.

## Donate

This project is maintained by a single person, so the documentation is not yet fully comprehensive and the feature set is still relatively limited.

If you would like to support the project, the most valuable help is:
- submitting pull requests
- actively opening issues when you encounter problems during usage

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.