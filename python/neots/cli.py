"""
neots - CLI tool for SingleLineTextGenerator.

Commands:
    neots init       Initialize a working directory with config and sample data
    neots generate   Generate OCR training images
    neots help       Show help information

flat-wiki - CLI tool for generating flattened Wikipedia corpuses.
"""

import argparse
import json
import os
import sys
import shutil
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Optional


def _get_data_path(filename: str) -> Optional[Path]:
    """Get path to a bundled data file."""
    try:
        ref = importlib_resources.files("neots") / "data" / filename
        if hasattr(ref, "__fspath__"):
            return Path(ref)
        return Path(str(ref))
    except Exception:
        return None


def _get_data_dir(dirname: str) -> Optional[Path]:
    """Get path to a bundled data directory."""
    try:
        ref = importlib_resources.files("neots") / "data" / dirname
        return Path(str(ref))
    except Exception:
        return None




def _copy_data_directory(src_dir: Path, dst_dir: str):
    """Copy a bundled data directory to the workspace."""
    dst = Path(dst_dir)
    if not src_dir.exists():
        print(f"  Warning: Bundled data directory not found: {src_dir}")
        return
    dst.mkdir(parents=True, exist_ok=True)
    for item in src_dir.iterdir():
        if item.is_file():
            dest_file = dst / item.name
            if not dest_file.exists():
                shutil.copy2(str(item), str(dest_file))


def cmd_init(args):
    """Handle 'neots init' command."""
    print("SingleLineTextGenerator Initialization")
    print("=" * 40)
    print()
    print("Choose initialization mode:")
    print("  [1] Preset configuration")
    print("      Includes multi-language corpus generation (zh/ja/ko/en/de/fr/it/es/ru)")
    print("      and symbol character files from the bundled templates.")
    print()
    print("  [2] Minimal configuration")
    print("      Creates only required directories and a minimal config.yaml.")
    print("      You'll need to prepare your own text corpuses and configure them manually.")
    print()

    while True:
        choice = input("Enter your choice \033[34m[1/2]\033[0m: ").strip()
        if choice in ("1", "2"):
            break
        print("\033[33mInvalid choice. Please enter 1 or 2.\033[0m")

    if choice == "1":
        _init_preset()
    else:
        _init_minimal()

    # Copy README
    readme_src = _get_data_path("NEOTS_README.md")
    if readme_src and readme_src.exists():
        shutil.copy2(str(readme_src), "NEOTS_README.md")
        print("  Copied NEOTS_README.md from bundled data")
    else:
        print("  Warning: NEOTS_README.md not found in bundled data")

    print()
    print("\033[32mInitialization complete!\033[0m")
    print("See NEOTS_README.md for detailed usage instructions.")


def _init_preset():
    """Initialize with preset configuration."""
    from neots import SingleLineTextGenerator, FlattenWikipediaGenerator

    print()
    print("Initializing with preset configuration...")
    print()

    # Create directories
    for d in ["corpuses", "fonts", "backgrounds", "generated"]:
        os.makedirs(d, exist_ok=True)
        print(f"  Created directory: {d}/")

    # Copy symbols_characters from bundled data
    symbols_src = _get_data_dir("symbols_characters")
    if symbols_src and symbols_src.exists():
        _copy_data_directory(symbols_src, "symbols_characters")
        print("  Copied symbols_characters/ from bundled data")
    else:
        os.makedirs("symbols_characters", exist_ok=True)
        print("  Created directory: symbols_characters/ (bundled data not found)")

    # Copy s2t.txt from bundled data
    s2t_src = _get_data_path("s2t.txt")
    if s2t_src and s2t_src.exists() and not os.path.exists("s2t.txt"):
        shutil.copy2(str(s2t_src), "s2t.txt")
        print("  Copied s2t.txt from bundled data")

    # Generate config.yaml from preset
    print("  Generating config.yaml ...")
    SingleLineTextGenerator.output_default_dict("config.yaml", type="yaml")
    print("  Created config.yaml")
    
    # Copy default font to fonts/ directory
    default_font = importlib_resources.files("neots") / "data" / "fonts" / "NotoSerifCJK-Regular.ttc"
    shutil.copy2(str(default_font), "fonts/NotoSerifCJK-Regular.ttc")
    
    print("  Copied default font to fonts/NotoSerifCJK-Regular.ttc")

    # Get vocab file path
    vocab_path = _get_data_path("default_dictionary.txt")
    vocab_str = str(vocab_path) if vocab_path and vocab_path.exists() else None

    if vocab_str:
        print(f"  Using vocabulary file: {vocab_str}")
    else:
        print("  Warning: Vocabulary file not found. Generating corpuses without vocab filter.")

    # Generate corpuses for each language
    languages = ["zh", "ja", "ko", "en", "de", "fr", "it", "es", "ru"]
    print()
    print("Generating text corpuses from Wikipedia (sample_count=1000, min_len=5)...")
    print("This requires internet access and may take a few minutes.")
    print()

    for lang in languages:
        output_path = f"./corpuses/{lang}.txt"
        if os.path.exists(output_path):
            print(f"  Skipping {lang} (already exists: {output_path})")
            continue

        try:
            gen = FlattenWikipediaGenerator(
                lang=lang,
                output=output_path,
                vocab=vocab_str,
                min_len=5,
                sample_count=1000,
            )
            gen.generate()
        except Exception as e:
            print(f"  Warning: Failed to generate corpus for {lang}: {e}")
            # Create an empty file so the config doesn't fail
            with open(output_path, "w", encoding="utf-8") as f:
                f.write("")
            print(f"  Created empty placeholder: {output_path}")


def _init_minimal():
    """Initialize with minimal configuration."""
    from neots import SingleLineTextGenerator

    print()
    print("Initializing with minimal configuration...")
    print()

    # Create only required directories
    for d in ["fonts", "backgrounds"]:
        os.makedirs(d, exist_ok=True)
        print(f"  Created directory: {d}/")

    # Generate config.yaml from minimal config
    SingleLineTextGenerator.output_minimal_dict("config.yaml", type="yaml")
    
    print("  Created config.yaml (minimal)")

    # Copy default font to fonts/ directory
    default_font = importlib_resources.files("neots") / "data" / "fonts" / "NotoSerifCJK-Regular.ttc"
    shutil.copy2(str(default_font), "fonts/NotoSerifCJK-Regular.ttc")
    
    print("  Copied default font to fonts/NotoSerifCJK-Regular.ttc")
    
    # Copy s2t.txt from bundled data
    s2t_src = _get_data_path("s2t.txt")
    if s2t_src and s2t_src.exists() and not os.path.exists("s2t.txt"):
        shutil.copy2(str(s2t_src), "s2t.txt")
        print("  Copied s2t.txt from bundled data")

def _validate_config(config_path: str) -> dict:
    """Validate a config file and return parsed config dict."""
    if not os.path.isfile(config_path):
        return None

    ext = os.path.splitext(config_path)[1].lower()
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            content = f.read()

        if ext in (".yaml", ".yml"):
            import yaml
            config = yaml.safe_load(content)
        else:
            config = json.loads(content)
    except Exception as e:
        raise ValueError(f"Failed to parse config file '{config_path}': {e}")

    if config is None:
        raise ValueError(f"Config file is empty: '{config_path}'")

    # Validate required sections
    for section in ["text_sampler", "random_config", "bg_sampler", "post_process", "generate"]:
        if section not in config:
            raise ValueError(f"Config missing required section: '{section}'")

    # Validate sequential items have valid from_file
    for item in config.get("random_config", []):
        if item.get("type") == "sequential" and "from_file" in item:
            fpath = item["from_file"]
            if not os.path.isfile(fpath):
                raise ValueError(
                    f"Sequential text file not found: '{fpath}'\n"
                    f"Please create this file or update the config."
                )

    return config


def cmd_generate(args):
    """Handle 'neots generate' command."""
    from neots import SingleLineTextGenerator

    config_path = args.config
    config_specified = args.config != "config.yaml"  # User explicitly specified

    # Validate config
    if not os.path.isfile(config_path):
        if config_specified:
            print(f"Error: Config file not found: {config_path}", file=sys.stderr)
            sys.exit(1)
        else:
            print(f"Error: Default config file '{config_path}' not found.", file=sys.stderr)
            print("Tip: Run 'neots init' to initialize a workspace with default configuration.",
                  file=sys.stderr)
            sys.exit(1)

    try:
        config = _validate_config(config_path)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        if not config_specified:
            print("Tip: Run 'neots init' to reinitialize the workspace.", file=sys.stderr)
        sys.exit(1)

    if config is None:
        print(f"Error: Config file not found: {config_path}", file=sys.stderr)
        sys.exit(1)

    total = args.total
    workers = args.workers

    if total <= 0:
        print("Error: --total must be a positive integer.", file=sys.stderr)
        sys.exit(1)

    print(f"Configuration: {config_path}")
    print(f"Total images:  {total}")
    print(f"Workers:       {workers if workers > 0 else 'auto'}")
    print()

    try:
        synth = SingleLineTextGenerator.from_config_file(config_path)
        synth.generate(total=total, workers=workers, show_progress=True)
    except Exception as e:
        print(f"Error during generation: {e}", file=sys.stderr)
        sys.exit(1)

    print()
    print("\033[32mGeneration complete!\033[0m")


def cmd_help(args=None):
    """Handle 'neots help' command."""
    print("""
SingleLineTextGenerator (neots) - OCR Training Data Generator

Usage:
    neots <command> [options]

Commands:
    init        Initialize a workspace with configuration and sample data
    generate    Generate OCR training images
    help        Show this help message

----------------------------------------------------------------------

neots init
    Interactive initialization wizard. Choose between:
    - Preset mode:  Generates multi-language corpuses from Wikipedia,
                    copies symbol files, and creates a full config.yaml.
    - Minimal mode: Creates empty directories and a minimal config.yaml
                    for manual setup.

----------------------------------------------------------------------

neots generate [options]
    Generate synthetic OCR training images.

    Options:
        --config PATH     Config file path (default: config.yaml)
        --total N         Number of images to generate (required)
        --workers N       Number of worker threads (default: auto)

    Examples:
        neots generate --total 10000
        neots generate --config my_config.yaml --total 50000 --workers 8

----------------------------------------------------------------------

neots help
    Show this help message.

----------------------------------------------------------------------

Python API:
    from neots import SingleLineTextGenerator

    synth = SingleLineTextGenerator.from_config_file("config.yaml")
    synth.generate(total=10000)

    img = synth.generate_instance("Hello", type="PIL")
""")


def main():
    """Main CLI entry point for 'neots'."""
    if len(sys.argv) < 2:
        cmd_help()
        sys.exit(0)

    command = sys.argv[1]

    if command == "init":
        parser = argparse.ArgumentParser(prog="neots init", add_help=False)
        args = parser.parse_args(sys.argv[2:])
        cmd_init(args)

    elif command == "generate":
        parser = argparse.ArgumentParser(prog="neots generate", add_help=False)
        parser.add_argument("--config", type=str, default="config.yaml",
                            help="Config file path (default: config.yaml)")
        parser.add_argument("--total", type=int, required=True,
                            help="Number of images to generate")
        parser.add_argument("--workers", type=int, default=0,
                            help="Number of worker threads (default: auto)")
        args = parser.parse_args(sys.argv[2:])
        cmd_generate(args)

    elif command == "help" or command == "--help" or command == "-h":
        cmd_help()

    else:
        print(f"Unknown command: {command}", file=sys.stderr)
        print("Run 'neots help' for usage information.", file=sys.stderr)
        sys.exit(1)


def flat_wiki_main():
    """Main CLI entry point for 'flat-wiki'."""
    parser = argparse.ArgumentParser(
        prog="flat-wiki",
        description="Generate flattened text corpuses from Wikipedia for OCR training.",
    )
    parser.add_argument(
        "--lang", "-l",
        type=str,
        required=True,
        help="Wikipedia language code (e.g., en, zh, ja, ko, de, fr).",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        required=True,
        help="Output file path for the generated corpus.",
    )
    parser.add_argument(
        "--vocab", "-v",
        type=str,
        default=None,
        help="Path to a vocabulary file for character filtering (optional).",
    )
    parser.add_argument(
        "--min_len",
        type=int,
        default=1,
        help="Minimum line length to include (default: 1).",
    )
    parser.add_argument(
        "--sample_count", "-n",
        type=int,
        default=None,
        help="Number of Wikipedia articles to process (default: all).",
    )

    args = parser.parse_args()

    from neots.flatten_wikipedia_generator import FlattenWikipediaGenerator

    gen = FlattenWikipediaGenerator(
        lang=args.lang,
        output=args.output,
        vocab=args.vocab,
        min_len=args.min_len,
        sample_count=args.sample_count,
    )

    try:
        gen.generate()
    except KeyboardInterrupt:
        print("\nAborted by user.", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()