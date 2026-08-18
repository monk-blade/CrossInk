#!/usr/bin/env python3
"""Generate a comprehensive Gujarati (Indic) test EPUB for CrossInk.

The book exercises the full Gujarati Unicode block (U+0A80-U+0AFF) and every
feature the in-firmware GujaratiShaper handles: independent vowels, the full
consonant set, the dependent-vowel "barakhadi", conjunct ligatures (joḍākṣar),
reph, subjoined/rakar Ra, anusvara, chandrabindu, visarga, nukta, digits, and
natural prose/poetry mixing all of the above. It also includes a mixed
Gujarati + Latin + Gujarati-digits chapter to check font fallback.

To render conjuncts correctly the reader must use a Gujarati SD font whose PUA
glyphs match lib/GujaratiShaper (e.g. a NotoSerifGujarati .cpfont built with
lib/EpdFont/scripts/fontconvert_sdcard.py --pua-mapping
lib/GujaratiShaper/scripts/pua_mapping.json).

Usage:
    python3 scripts/generate_gujarati_test_epub.py [output.epub]
"""

from __future__ import annotations

import sys
import zipfile
from pathlib import Path

TITLE = "ગુજરાતી યુનિકોડ પરીક્ષણ"  # "Gujarati Unicode Test"
AUTHOR = "CrossInk"
LANG = "gu"

CSS = """\
body { margin: 0; padding: 0; line-height: 1.5; }
h1 { font-size: 1.4em; text-align: center; margin: 0.6em 0; }
h2 { font-size: 1.2em; margin: 0.8em 0 0.3em 0; }
p  { margin: 0.4em 0; text-align: justify; }
.center { text-align: center; }
.grid { line-height: 1.9; }
.big { font-size: 1.25em; }
.label { font-weight: bold; }
.verse { text-align: center; font-style: italic; line-height: 1.7; margin: 0.6em 0; }
"""


def chapter_intro() -> tuple[str, str]:
    body = """
<h1>ગુજરાતી યુનિકોડ પરીક્ષણ</h1>
<p class="center">CrossInk રીડર માટે સંપૂર્ણ ઇન્ડિક પરીક્ષણ પુસ્તક</p>
<p>આ પુસ્તક ગુજરાતી લિપિના તમામ યુનિકોડ અક્ષરો, માત્રાઓ, જોડાક્ષરો,
રેફ, અનુસ્વાર, ચંદ્રબિંદુ, વિસર્ગ અને અંકોનું પરીક્ષણ કરવા માટે તૈયાર
કરવામાં આવ્યું છે. જો ફોન્ટ યોગ્ય રીતે સ્થાપિત હોય, તો દરેક અક્ષર અને
જોડાક્ષર બરાબર દેખાવો જોઈએ — કોઈ ખાલી ચોરસ (ટોફૂ) ન દેખાવા જોઈએ.</p>
<p class="verse">વાંચન એ જ્ઞાનનું પ્રવેશદ્વાર છે.<br/>
પુસ્તકો આપણા સાચા મિત્રો છે.</p>
"""
    return ("પ્રસ્તાવના", body)


def chapter_vowels() -> tuple[str, str]:
    body = """
<h1>સ્વર — Independent Vowels</h1>
<p class="grid big">અ  આ  ઇ  ઈ  ઉ  ઊ  ઋ  ૠ  ઌ  ૡ  ઍ  એ  ઐ  ઑ  ઓ  ઔ</p>
<h2>સ્વર સાથે ઉદાહરણ</h2>
<p>અમદાવાદ · આકાશ · ઇતિહાસ · ઈશ્વર · ઉજ્જૈન · ઊર્જા · ઋષિ ·
એકતા · ઐરાવત · ઓજસ · ઔષધ</p>
"""
    return ("સ્વર", body)


def chapter_consonants() -> tuple[str, str]:
    body = """
<h1>વ્યંજન — Consonants</h1>
<p class="grid big">ક  ખ  ગ  ઘ  ઙ</p>
<p class="grid big">ચ  છ  જ  ઝ  ઞ</p>
<p class="grid big">ટ  ઠ  ડ  ઢ  ણ</p>
<p class="grid big">ત  થ  દ  ધ  ન</p>
<p class="grid big">પ  ફ  બ  ભ  મ</p>
<p class="grid big">ય  ર  લ  ળ  વ</p>
<p class="grid big">શ  ષ  સ  હ</p>
<p class="grid big">ક્ષ  જ્ઞ</p>
"""
    return ("વ્યંજન", body)


def chapter_barakhadi() -> tuple[str, str]:
    # Dependent vowel signs (matras) applied to KA, including pre-base i.
    body = """
<h1>બારાક્ષરી — Matras (Dependent Vowels)</h1>
<p>વ્યંજન 'ક' સાથે તમામ માત્રાઓ:</p>
<p class="grid big">ક  કા  કિ  કી  કુ  કૂ  કૃ  કૄ  કૅ  કે  કૈ  કૉ  કો  કૌ  કં  કઃ  ક્</p>
<p>'મ' સાથે બારાક્ષરી:</p>
<p class="grid big">મ  મા  મિ  મી  મુ  મૂ  મે  મૈ  મો  મૌ  મં  મઃ</p>
<h2>પ્રી-બેઝ 'િ' માત્રા (reordered)</h2>
<p>કિ · ગિ · મિત્ર · સ્થિતિ · શિક્ષણ · દિવસ · વિદ્યા · હિંમત</p>
"""
    return ("બારાક્ષરી", body)


def chapter_conjuncts() -> tuple[str, str]:
    body = """
<h1>જોડાક્ષર — Conjuncts (Ligatures)</h1>
<p class="grid big">ક્ષ · જ્ઞ · ત્ર · શ્ર · દ્ય · દ્વ · હ્મ · ક્ત · સ્ત · સ્થ ·
ન્ન · ટ્ટ · ડ્ડ · ન્દ · ન્ધ · ષ્ટ · શ્ચ · ગ્ધ · ત્ય · દ્દ</p>
<h2>જોડાક્ષરવાળા શબ્દો</h2>
<p>લક્ષ્મી · વિદ્યા · જ્ઞાન · પ્રશ્ન · ક્ષત્રિય · વિદ્યાર્થી ·
પુસ્તક · સત્ય · વિશ્વ · અધ્યાપક · સ્વાસ્થ્ય · ઉત્સાહ ·
મહત્ત્વ · સંસ્કૃત · શાસ્ત્ર · અભ્યાસ</p>
"""
    return ("જોડાક્ષર", body)


def chapter_reph_rakar() -> tuple[str, str]:
    body = """
<h1>રેફ અને રકાર — Reph &amp; Subjoined Ra</h1>
<h2>રેફ (ર્ + વ્યંજન)</h2>
<p>ધર્મ · કર્મ · અર્થ · સૂર્ય · વર્ષ · દર્દ · સ્વર્ગ · પૂર્ણ ·
ગર્વ · નિર્ણય · પરિવર્તન · સંઘર્ષ · કર્યું · થર્મોમીટર</p>
<h2>રકાર / જોડાયેલ 'ર' (વ્યંજન + ્ + ર)</h2>
<p>પ્ર · ક્ર · ત્ર · ગ્ર · દ્ર · ભ્ર · શ્ર · સ્ત્ર · ટ્ર</p>
<p>પ્રેમ · ક્રમ · ગ્રંથ · ચંદ્ર · રાષ્ટ્ર · મિત્ર · પુત્ર ·
પ્રકાશ · વિક્રમ · ઇન્દ્ર · શ્રમ · ત્રણ</p>
"""
    return ("રેફ અને રકાર", body)


def chapter_marks() -> tuple[str, str]:
    body = """
<h1>અનુસ્વાર · ચંદ્રબિંદુ · વિસર્ગ</h1>
<h2>અનુસ્વાર ( ં )</h2>
<p>સંઘ · રંગ · ગંગા · સિંહ · બિંદુ · હિંમત · અંક · મંદિર ·
ચિંતન · સંસ્કાર · અંગ · સંગીત</p>
<h2>ચંદ્રબિંદુ ( ઁ )</h2>
<p>અઁ · ઇઁ · માઁ · ગાઁઠ · કઁ · શ્રીઁ</p>
<h2>વિસર્ગ ( ઃ )</h2>
<p>દુઃખ · પ્રાતઃ · અંતઃકરણ · પુનઃ · નમઃ</p>
<h2>નુક્તા અને અવગ્રહ</h2>
<p>ઽ (અવગ્રહ) · જ઼ · ડ઼ · ફ઼</p>
"""
    return ("ચિહ્નો", body)


def chapter_digits() -> tuple[str, str]:
    body = """
<h1>અંક અને પ્રતીક — Digits &amp; Symbols</h1>
<h2>ગુજરાતી અંક</h2>
<p class="grid big">૦  ૧  ૨  ૩  ૪  ૫  ૬  ૭  ૮  ૯</p>
<p>૧૨૩૪૫૬૭૮૯૦ · ઈ.સ. ૨૦૨૬ · ₹ ૧,૫૦૦ · ૯૯% · ૩.૧૪</p>
<h2>પ્રતીક</h2>
<p class="big">ૐ · ઽ · ૱ · ।  ॥</p>
<p class="center">ૐ નમઃ શિવાય</p>
"""
    return ("અંક અને પ્રતીક", body)


def chapter_prose() -> tuple[str, str]:
    body = """
<h1>ગદ્ય — A Gujarati Passage</h1>
<p>ગુજરાતી ભાષા ભારત દેશના ગુજરાત રાજ્યની રાજભાષા છે. તે ઇન્ડો-આર્યન
ભાષાકુળની ભાષા છે અને દેવનાગરી સાથે સંબંધ ધરાવતી ગુજરાતી લિપિમાં
લખવામાં આવે છે. વિશ્વભરમાં લગભગ છ કરોડ લોકો ગુજરાતી બોલે છે.</p>
<p>પુસ્તકો માણસના સૌથી સારા મિત્રો છે. વાંચનથી જ્ઞાન વધે છે, વિચારો
વિશાળ બને છે અને મન શાંત થાય છે. જે વ્યક્તિ નિયમિત વાંચે છે તે
હંમેશાં નવું શીખતી રહે છે. તેથી દરરોજ થોડું વાંચવાની ટેવ પાડવી જોઈએ.</p>
<p>સૂર્યનાં કિરણો ધીરે ધીરે પર્વત પર પડ્યાં. પક્ષીઓ મધુર સ્વરે ગાવા
લાગ્યાં. નદીના શાંત પ્રવાહમાં આકાશનું પ્રતિબિંબ ઝગમગતું હતું. પ્રકૃતિની
આ સુંદરતા જોઈને હૃદય આનંદથી ભરાઈ ગયું.</p>
<h2>કાવ્ય</h2>
<p class="verse">જ્યાં જ્યાં વસે એક ગુજરાતી,<br/>
ત્યાં ત્યાં સદાકાળ ગુજરાત!<br/>
જ્યાં જ્યાં ગુજરાતી બોલાતી,<br/>
ત્યાં ત્યાં ગુર્જરીની મહેકાત.</p>
"""
    return ("ગદ્ય અને કાવ્ય", body)


def chapter_mixed() -> tuple[str, str]:
    body = """
<h1>મિશ્ર લખાણ — Mixed Script</h1>
<p>CrossInk રીડર પર Gujarati EPUB ૧૦૦% બરાબર કામ કરે છે. This paragraph
mixes English, ગુજરાતી, and Gujarati digits (૨૦૨૬) in one line to verify
font fallback and shaping together.</p>
<p><span class="label">પરીક્ષણ સૂચિ:</span> vowels •, consonants •,
matras •, conjuncts (જોડાક્ષર) •, reph (ધર્મ) •, rakar (પ્ર) •,
anusvara (સંઘ) •, digits (૯) •.</p>
<p class="center">— સમાપ્ત / The End —</p>
"""
    return ("મિશ્ર લખાણ", body)


def make_xhtml(title: str, body: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="{LANG}" lang="{LANG}">
<head>
  <meta charset="utf-8"/>
  <title>{title}</title>
  <link rel="stylesheet" type="text/css" href="styles/style.css"/>
</head>
<body>
{body}
</body>
</html>
"""


def create_epub(epub_path: Path, chapters: list[tuple[str, str]]) -> None:
    with zipfile.ZipFile(epub_path, "w", zipfile.ZIP_DEFLATED) as epub:
        # mimetype must be the first entry and stored uncompressed.
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)

        epub.writestr(
            "META-INF/container.xml",
            """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>""",
        )

        epub.writestr("OEBPS/styles/style.css", CSS)

        manifest = [
            '    <item id="css" href="styles/style.css" media-type="text/css"/>',
            '    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
        ]
        spine = []
        for i, (title, body) in enumerate(chapters):
            cid = f"chapter{i + 1}"
            fname = f"{cid}.xhtml"
            manifest.append(
                f'    <item id="{cid}" href="{fname}" media-type="application/xhtml+xml"/>'
            )
            spine.append(f'    <itemref idref="{cid}"/>')
            epub.writestr(f"OEBPS/{fname}", make_xhtml(title, body))

        nl = "\n"
        epub.writestr(
            "OEBPS/content.opf",
            f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">crossink-gujarati-indic-test</dc:identifier>
    <dc:title>{TITLE}</dc:title>
    <dc:creator>{AUTHOR}</dc:creator>
    <dc:language>{LANG}</dc:language>
  </metadata>
  <manifest>
{nl.join(manifest)}
  </manifest>
  <spine>
{nl.join(spine)}
  </spine>
</package>""",
        )

        nav_items = nl.join(
            f'      <li><a href="chapter{i + 1}.xhtml">{chapters[i][0]}</a></li>'
            for i in range(len(chapters))
        )
        epub.writestr(
            "OEBPS/nav.xhtml",
            f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="{LANG}" lang="{LANG}">
<head><meta charset="utf-8"/><title>અનુક્રમણિકા</title></head>
<body>
  <nav epub:type="toc">
    <h1>અનુક્રમણિકા</h1>
    <ol>
{nav_items}
    </ol>
  </nav>
</body>
</html>""",
        )


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path(__file__).resolve().parents[1] / "test" / "epubs" / "test_gujarati_indic.epub"
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    chapters = [
        chapter_intro(),
        chapter_vowels(),
        chapter_consonants(),
        chapter_barakhadi(),
        chapter_conjuncts(),
        chapter_reph_rakar(),
        chapter_marks(),
        chapter_digits(),
        chapter_prose(),
        chapter_mixed(),
    ]
    create_epub(out, chapters)
    print(f"Wrote {out} ({out.stat().st_size} bytes, {len(chapters)} chapters)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
