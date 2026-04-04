"""
FlattenWikipediaGenerator - Generate flattened text corpuses from Wikipedia.

Uses Hugging Face's streaming API to download and process Wikipedia articles
into flat text files suitable for OCR training data generation.
"""

import random
import re
from typing import Optional, Set


class FlattenWikipediaGenerator:
    """
    Generator that creates flattened text files from Wikipedia articles.

    Downloads Wikipedia articles via Hugging Face's streaming API and
    processes them into a single flat text file with cleaned lines joined
    by spaces.

    Parameters
    ----------
    lang : str
        Language code (e.g., "en", "zh", "ja", "ko", "de", "fr", etc.).
        The Wikipedia subset will be "20231101.<lang>".
    output : str
        Path to the output text file.
    vocab : str or set, optional
        Path to a vocabulary file, or a set of allowed characters.
        If specified, only lines containing exclusively these characters
        will be included.
    min_len : int, optional
        Minimum length of a cleaned line to be included (default: 1).
    sample_count : int, optional
        If specified, only process the first `sample_count` articles.
    """

    WIKIPEDIA_TIMESTAMP = "20231101"

    def __init__(
        self,
        lang: str,
        output: str,
        vocab: Optional[str] = None,
        min_len: int = 1,
        sample_count: Optional[int] = None,
    ):
        self.lang = lang
        self.subset = f"{self.WIKIPEDIA_TIMESTAMP}.{lang}"
        self.output = output
        self.min_len = min_len
        self.sample_count = sample_count

        # Load vocabulary
        self.vocab_set: Optional[Set[str]] = None
        if vocab is not None:
            if isinstance(vocab, set):
                self.vocab_set = vocab
            elif isinstance(vocab, str):
                self.vocab_set = self._load_vocab(vocab)
            else:
                raise TypeError(f"vocab must be str path or set, got {type(vocab)}")

    @staticmethod
    def _load_vocab(vocab_path: str) -> Set[str]:
        """Load a vocabulary file (one character per line or all chars in file)."""
        with open(vocab_path, "r", encoding="utf-8") as f:
            return {line.strip("\n") for line in f if line.strip("\n")}

    @staticmethod
    def _contains_only_vocab(text: str, vocab_set: Set[str]) -> bool:
        """Check if text contains only characters in the vocabulary."""
        return all(char in vocab_set for char in text)

    def generate(self):
        """
        Run the generation process.

        Downloads Wikipedia articles via streaming, cleans and filters lines,
        then writes the output file.
        """
        try:
            from datasets import load_dataset
        except ImportError:
            raise ImportError(
                "The 'datasets' package is required for FlattenWikipediaGenerator. "
                "Install it with: pip install datasets"
            )

        try:
            from tqdm import tqdm
        except ImportError:
            # Fallback: no progress bar
            tqdm = None

        print(f"Streaming Wikipedia subset: {self.subset} ...")

        if self.vocab_set:
            print(f"Vocabulary filter active: {len(self.vocab_set)} unique characters")

        ds = load_dataset(
            "wikimedia/wikipedia", self.subset, split="train", streaming=True
        )

        unique_datalines: set = set()
        spaces_pattern = re.compile(r"\s+")

        max_samples = self.sample_count if self.sample_count else float("inf")

        if tqdm and self.sample_count:
            pbar = tqdm(total=self.sample_count, desc=f"Processing [{self.lang}]")
        else:
            pbar = None

        count = 0
        for entry in ds:
            if count >= max_samples:
                break

            text = entry["text"]
            raw_lines = text.splitlines()

            for line in raw_lines:
                clean_line = spaces_pattern.sub(" ", line).strip()
                if len(clean_line) < self.min_len:
                    continue
                if self.vocab_set and not self._contains_only_vocab(
                    clean_line, self.vocab_set
                ):
                    continue
                unique_datalines.add(clean_line)

            count += 1
            if pbar:
                pbar.update(1)

        if pbar:
            pbar.close()

        print(
            f"[{self.lang}] Filtering complete. "
            f"Unique lines: {len(unique_datalines)}. Shuffling..."
        )

        final_list = list(unique_datalines)
        random.shuffle(final_list)

        with open(self.output, "w", encoding="utf-8") as f:
            f.write(" ".join(final_list))

        print(f"[{self.lang}] Done. File saved to {self.output}")