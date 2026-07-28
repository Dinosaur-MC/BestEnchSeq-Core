"""Language configuration: key prefixes, locale normalisation, defaults."""

# ── key prefixes to extract from Minecraft language files ─────────────────

RELEVANT_PREFIXES: tuple[str, ...] = (
    "enchantment.minecraft.",    # enchantment display names
    "item.minecraft.",           # item / equipment names
)

# ── locale helpers ────────────────────────────────────────────────────────


def locale_display(locale: str) -> str:
    """Normalise a Minecraft locale code to display form.

    Simple rule: ``language_REGION`` → ``language_REGION``
    (language lowercased, region uppercased).  Anything that doesn't
    match the ``xx_XX`` pattern is returned as-is.

    Examples
    --------
    en_us  →  en_US
    zh_cn  →  zh_CN
    ja_jp  →  ja_JP
    bar    →  bar          (three-letter code, no region)
    enp    →  enp          (constructed language, no region)
    """
    parts = locale.split("_")
    if len(parts) == 2 and len(parts[0]) >= 2 and len(parts[1]) >= 2:
        return f"{parts[0].lower()}_{parts[1].upper()}"
    return locale


def locale_to_minecraft(display: str) -> str:
    """Reverse of ``locale_display``: ``en_US`` → ``en_us``."""
    return display.lower()


# ── defaults ──────────────────────────────────────────────────────────────

DEFAULT_LOCALES = ("en_us", "zh_cn")
