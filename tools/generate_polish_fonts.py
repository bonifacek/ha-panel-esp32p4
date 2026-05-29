from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = ROOT / "managed_components" / "lvgl__lvgl" / "scripts" / "built_in_font" / "Montserrat-Medium.ttf"
OUT_DIR = ROOT / "main" / "fonts"

POLISH_CHARS = "ĄĆĘŁŃÓŚŹŻąćęłńóśźż"
SIZES = {
    18: {"line_height": 21, "base_line": 4, "fallback": "lv_font_montserrat_18"},
    20: {"line_height": 22, "base_line": 4, "fallback": "lv_font_montserrat_20"},
    22: {"line_height": 24, "base_line": 4, "fallback": "lv_font_montserrat_22"},
    24: {"line_height": 27, "base_line": 5, "fallback": "lv_font_montserrat_24"},
}


def pack_4bpp(pixels):
    out = []
    for i in range(0, len(pixels), 2):
        high = pixels[i] >> 4
        low = pixels[i + 1] >> 4 if i + 1 < len(pixels) else 0
        out.append((high << 4) | low)
    return out


def glyph_for(font, char):
    bbox = font.getbbox(char, anchor="ls")
    left, top, right, bottom = bbox
    width = max(0, right - left)
    height = max(0, bottom - top)
    adv_w = int(round(font.getlength(char) * 16))

    if width == 0 or height == 0:
        return {
            "bitmap": [],
            "adv_w": adv_w,
            "box_w": 0,
            "box_h": 0,
            "ofs_x": 0,
            "ofs_y": 0,
        }

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-left, -top), char, font=font, fill=255, anchor="ls")

    bitmap = pack_4bpp(list(image.getdata()))
    return {
        "bitmap": bitmap,
        "adv_w": adv_w,
        "box_w": width,
        "box_h": height,
        "ofs_x": left,
        "ofs_y": -bottom,
    }


def c_array(values, indent="    ", per_line=12):
    if not values:
        return ""
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i : i + per_line]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def generate_font(size, info):
    font = ImageFont.truetype(str(FONT_PATH), size)
    chars = sorted(POLISH_CHARS, key=ord)
    glyphs = [glyph_for(font, char) for char in chars]

    bitmap = []
    for glyph in glyphs:
        glyph["bitmap_index"] = len(bitmap)
        bitmap.extend(glyph["bitmap"])

    min_cp = ord(chars[0])
    max_cp = ord(chars[-1])
    unicode_offsets = [ord(char) - min_cp for char in chars]
    name = f"lv_font_polish_{size}"

    glyph_dsc_lines = [
        "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"
    ]
    for glyph in glyphs:
        glyph_dsc_lines.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (
                glyph["bitmap_index"],
                glyph["adv_w"],
                glyph["box_w"],
                glyph["box_h"],
                glyph["ofs_x"],
                glyph["ofs_y"],
            )
        )

    unicode_lines = []
    for i in range(0, len(unicode_offsets), 12):
        chunk = unicode_offsets[i : i + 12]
        unicode_lines.append("    " + ", ".join(f"0x{value:x}" for value in chunk) + ",")

    content = f"""#include "lvgl.h"

#if LV_FONT_MONTSERRAT_{size}

LV_FONT_DECLARE({info["fallback"]});

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{
{c_array(bitmap)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(glyph_dsc_lines)}
}};

static const uint16_t unicode_list_0[] = {{
{chr(10).join(unicode_lines)}
}};

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{
        .range_start = {min_cp}, .range_length = {max_cp - min_cp + 1}, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = {len(chars)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }}
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {info["line_height"]},
    .base_line = {info["base_line"]},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = &{info["fallback"]},
}};

#endif
"""

    (OUT_DIR / f"{name}.c").write_text(content, encoding="utf-8")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for size, info in SIZES.items():
        generate_font(size, info)


if __name__ == "__main__":
    main()
