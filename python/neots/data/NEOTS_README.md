# NeoTextSynthesizer Workspace

Welcome to your NeoTextSynthesizer workspace! This README will help you get started
with generating synthetic OCR training data.

## Directory Structure

```
.
├── config.yaml              # Main configuration file
├── corpuses/                # Text corpus files (one per language)
│   ├── zh.txt              # Chinese corpus
│   ├── en.txt              # English corpus
│   └── ...                 # Other languages
├── fonts/                   # Font files (.ttf/.otf/.ttc)
├── backgrounds/             # Background images for generation
├── symbols_characters/      # Character/symbol definition files
├── generated/               # Output directory for generated images
└── generated.jsonl          # Output annotation file
```

## Configuration Guide

### `text_sampler` Section

Controls the basic text sampling parameters and font selection strategy.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `min_targets` | 5 | Minimum characters per image |
| `max_targets` | 50 | Maximum characters per image |
| `font_size` | 55 | Base font size in pixels |
| `sample_strategy` | `"font-first"` | Strategy for handling missing characters: `"font-first"`, `"sample-first"`, or `"auto-fallback"` |
| `font_list` | `["./fonts"]` | List of font files or directories containing fonts |

#### Sample Strategies
- **`font-first`**: Selects a font first, then samples text. Any characters not supported by the chosen font are discarded.
  > Note: This strategy may produce shorter text, and **fail generations** if none of the characters are supported in the chosen font.
- **`sample-first`**: Samples text first, then finds a font that supports all characters. If none exists, chooses the font with the highest coverage and discards unsupported characters.
- **`auto-fallback`**: Selects a primary font and samples text. For unsupported characters, it attempts to find a fallback font to render them. **auto-fallback is the default strategy and we recommend using it.**

### `random_config` Section

An array that defines how text is sampled for image generation.
Each entry has a `type`, a `prob` (probability weight), and a data source.

#### Sequential Type
Reads text sequentially from a flat text file. Best for natural language corpuses.

```yaml
random_config:
  - type: sequential
    prob: 0.20
    from_file: ./corpuses/zh.txt
```

#### Random Type
Randomly samples characters from files or sub-groups. Best for character/symbol sets.

```yaml
  - type: random
    prob: 0.20
    sub_items:
      - prob: 0.4
        from_file: ./symbols_characters/characters.txt
        section: [null, 3500]    # Use only first 3500 lines
      - prob: 0.3
        from_file: ./symbols_characters/symbols.txt
      - prob: 0.1
        from_string: " "         # Inline character source
```

#### Key Parameters

| Parameter | Description |
|-----------|-------------|
| `type` | `"sequential"` (stream from file) or `"random"` (random sample) |
| `prob` | Probability weight (all probs are auto-normalized to sum to 1.0) |
| `from_file` | Path to a text file |
| `from_string` | Inline text source (e.g., a space character) |
| `section` | `[start, end]` - Use only a slice of lines. `null` means beginning/end |
| `sub_items` | Nested sampling groups (recursive structure) |
| `traditional_prob` | Probability of converting simplified Chinese to traditional |

### `bg_sampler` Section

Controls the background selection.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `bg_image_prob` | 0.3 | Probability of using a real background image |
| `gray_bg_prob` | 0.7 | Probability of using a gray background (vs colored) |
| `text_color` | `"auto"` | Text color (can be `"auto"`, `"#RRGGBB"` hex format, or a range like `["#000000", "#FFFFFF"]`) |
| `bg_color` | `"auto"` | Background color (can be `"auto"`, `"#RRGGBB"` hex format, or a range like `["#000000", "#FFFFFF"]`) |
| `bg_list` | `["./backgrounds"]` | List of background image files or directories |

### `post_process` Section

Controls the visual appearance of generated images.

#### `text_effects`
| Parameter | Default | Description |
|-----------|---------|-------------|
| `effect_prob` | 0.2 | Probability of applying text effects |
| `partial_effect_prob` | 0.8 | Probability that effects apply to only part of the text |

#### `text_paste`
| Parameter | Default | Description |
|-----------|---------|-------------|
| `scale_range` | [0.7, 1.1] | Random scale factor range |
| `margin_range` | [-5, 20] | Random margin range in pixels |
| `offset_prob` | 0.5 | Probability of applying position offset |
| `h_offset_range` | [-7, 7] | Horizontal offset range |
| `v_offset_range` | [-5, 5] | Vertical offset range |

#### `transforms`
| Parameter | Default | Description |
|-----------|---------|-------------|
| `rotation_prob` | 0.3 | Probability of applying rotation |
| `rotation_range` | [-2.5, 2.5] | Rotation angle range in degrees |
| `distortion_prob` | 0.2 | Probability of applying distortion |
| `distortion_level` | 0.03 | Distortion intensity (e.g., 0.03 means 3% distortion) |

### `generate` Section

Controls the output structure.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `output_height` | 48 | Output image height in pixels |
| `retry_on_error` | true | Whether to retry generation on error |
| `out_jsonl` | ./generated.jsonl | Output annotation file path |
| `out_dir` | ./generated | Output image directory |
| `batchsize` | 10000 | JSONL write batch size |
| `hierarchical_structure` | [100, 625] | Directory nesting structure |

## Fonts Setup

Place your font files in the `fonts/` directory. The system will automatically index them and use them according to your chosen `sample_strategy`.
The more diverse your font collection, the better your OCR model will generalize.

**Recommended:** Collect 50-200+ diverse fonts covering your target scripts.

**If no fonts are provided**, the system will attempt to use the bundled default font (`NotoSerifCJK-Regular.ttc`).

## Background Images

For more realistic training data, add background images to the `backgrounds/` directory.

**Recommended sources:**
- **ImageNet-1k (IN-1k)**: Sample diverse real-world images
- **OpenImages**: Large-scale dataset with varied scenes
- **COCO**: Common Objects in Context dataset
- **Your own data**: Screenshots, document scans, photos, etc.

Then set `bg_image_prob` in `bg_sampler` to control how often real backgrounds are used
(e.g., `0.3` means 30% of images use a real background, 70% use synthetic colors).

**Supported formats:** `.jpg`, `.jpeg`, `.png`, `.bmp`, `.webp`

## Quick Start

```bash
# Generate 10,000 single-line text images with auto-detected worker count
neots singleline --total 10000

# Generate with custom config and worker count
neots singleline --config my_config.yaml --total 50000 --workers 8
```

## Python API

```python
from neots import SingleLineTextGenerator

# Initialize with default config
synth = SingleLineTextGenerator.from_config_file("config.yaml")

# Batch generate
generated, failed = synth.generate(total=10000, workers=8)
# print("generated:", generated)
# print("failed:", failed)

# Generate single image
img = synth.generate_instance("Hello World", type="PIL")