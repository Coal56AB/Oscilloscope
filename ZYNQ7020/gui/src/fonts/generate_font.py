"""Generate the portable C glyph atlas. Requires Pillow only when regenerating."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


HERE = Path(__file__).resolve().parent
CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-+/*x_uskzm%"
SIZES = ((2, 18), (3, 27), (4, 36))
SUPERSAMPLE = 8
FONTS = (("inter", "Inter-Medium.ttf"),)


def glyph(font_path, char, scale, font_size):
    height = 7 * scale
    font = ImageFont.truetype(str(font_path), font_size * SUPERSAMPLE)
    advance = max(1, int(round(font.getlength(char) / SUPERSAMPLE)))
    canvas = Image.new("L", (60 * SUPERSAMPLE, height * SUPERSAMPLE))
    draw = ImageDraw.Draw(canvas)
    draw.text(
        (4 * SUPERSAMPLE, (height - 1) * SUPERSAMPLE),
        char,
        font=font,
        fill=255,
        anchor="ls",
    )
    canvas = canvas.resize((60, height), Image.LANCZOS)
    bounds = canvas.getbbox()
    if bounds is None:
        return 0, 0, 0, 0, advance, []
    left, top, right, bottom = bounds
    pixels = list(canvas.crop(bounds).getdata())
    return left - 4, top, right - left, bottom - top, advance, pixels


def main():
    lines = [
        "/* Generated from the redistributable fonts in src/fonts/; see their licenses. */",
        "#ifndef SCOPE_FONT_SMOOTH_H",
        "#define SCOPE_FONT_SMOOTH_H",
        "#include <stdint.h>",
        "typedef struct { uint32_t offset; uint8_t width, height, advance; "
        "int8_t left, top; } SmoothGlyph;",
    ]
    for identifier, filename in FONTS:
        font_path = HERE / filename
        if not font_path.is_file():
            raise FileNotFoundError(font_path)
        for scale, font_size in SIZES:
            data = []
            glyphs = []
            for char in CHARS:
                left, top, width, height, advance, pixels = glyph(
                    font_path, char, scale, font_size
                )
                glyphs.append((len(data), width, height, advance, left, top))
                data.extend(pixels)
            lines.append(
                f"static const SmoothGlyph {identifier}_glyphs_{scale}"
                f"[{len(CHARS)}] = {{"
            )
            for char, values in zip(CHARS, glyphs):
                lines.append("    /* " + repr(char) + " */ {" +
                             ",".join(str(value) for value in values) + "},")
            lines.append("};")
            lines.append(f"static const uint8_t {identifier}_alpha_{scale}[] = {{")
            for start in range(0, len(data), 20):
                lines.append("    " + ",".join(
                    f"{value:3d}" for value in data[start:start + 20]
                ) + ",")
            lines.append("};")
    lines.extend(("#endif", ""))
    (HERE.parent / "scope_font_smooth.h").write_text(
        "\n".join(lines), encoding="ascii"
    )


if __name__ == "__main__":
    assert len(CHARS) == 51
    main()
