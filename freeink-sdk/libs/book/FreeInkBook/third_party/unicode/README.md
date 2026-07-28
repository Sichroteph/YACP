Trimmed Unicode Character Database extracts (UCD 17.0) used by
tools/arabshapec.py to generate include/text/arab_shaping.h:

- ArabicShaping.txt — verbatim from
  https://www.unicode.org/Public/UCD/latest/ucd/ArabicShaping.txt
- ArabicPresentationForms.txt — UnicodeData.txt lines for U+FB50..FEFF with
  <isolated>/<final>/<initial>/<medial> compatibility decompositions
- ArabicBlockCategories.txt — cp;name;category for U+0600..077F (identifies
  Mn/Me/Cf characters, which are joining-type Transparent per the
  ArabicShaping.txt header note)

License: Unicode License v3 (https://www.unicode.org/license.txt).
