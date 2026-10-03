"""Turkish phrasing helpers: clock times as words with correct case suffixes.

    at_time(7, 24)    -> "saat yedi yirmi dörtte"
    until_time(7, 4)  -> "saat yedi dörde kadar"
    at_time(7, 30)    -> "saat yedi buçukta"
"""

ONES = ["", "bir", "iki", "üç", "dört", "beş", "altı", "yedi", "sekiz", "dokuz"]
TENS = ["", "on", "yirmi", "otuz", "kırk", "elli"]
BACK_VOWELS = set("aıou")
FRONT_VOWELS = set("eiöü")
HARD_CONSONANTS = set("fstkçşhp")  # "fıstıkçı şahap"


def number(n: int) -> str:
    if n == 0:
        return "sıfır"
    words = [TENS[n // 10], ONES[n % 10]]
    return " ".join(w for w in words if w)


def _last_vowel(word: str) -> str:
    for ch in reversed(word):
        if ch in BACK_VOWELS:
            return "a"
        if ch in FRONT_VOWELS:
            return "e"
    return "e"


def locative(word: str) -> str:
    """-de/-da/-te/-ta"""
    cons = "t" if word[-1] in HARD_CONSONANTS else "d"
    return word + cons + _last_vowel(word)


def dative(word: str) -> str:
    """-e/-a/-ye/-ya, with the consonant softening numbers need (dört -> dörde, buçuk -> buçuğa)."""
    v = _last_vowel(word)
    if word[-1] in BACK_VOWELS | FRONT_VOWELS:
        return word + "y" + v
    if word.endswith("dört"):
        return word[:-1] + "d" + v
    if word.endswith("buçuk"):
        return word[:-1] + "ğ" + v
    return word + v


def _clock_words(h: int, m: int) -> list[str]:
    words = ["saat", number(h)]
    if m == 30:
        words.append("buçuk")
    elif m:
        if m < 10:
            words.append("sıfır")  # 07:03 -> "yedi sıfır üç", as digital clocks are read
        words.append(number(m))
    return " ".join(words).split()


def at_time(h: int, m: int) -> str:
    w = _clock_words(h, m)
    return " ".join(w[:-1] + [locative(w[-1])])


def until_time(h: int, m: int) -> str:
    w = _clock_words(h, m)
    return " ".join(w[:-1] + [dative(w[-1])]) + " kadar"


def parse_hhmm(s: str) -> tuple[int, int]:
    h, m = s.split(":")
    return int(h), int(m)


if __name__ == "__main__":
    for h, m in [(7, 24), (7, 3), (7, 30), (19, 9), (6, 40), (18, 41), (7, 0), (23, 4)]:
        print(f"{h:02d}:{m:02d}", "|", at_time(h, m), "|", until_time(h, m))
