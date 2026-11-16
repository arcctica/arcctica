#!/usr/bin/env python3
"""End-to-end check of mc1h_strip on real JPEG/PNG files made with Pillow.

usage: python3 -I tests/e2e_strip.py <path-to-mc1h_strip> <work-dir>

For every case: decoded pixels before == after (no re-encoding), the image
displays the same (exif_transpose equal), orientation kept, GPS/camera/date,
comments, XMP and PNG text removed, ICC profile kept.
"""
import io
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageCms, ImageOps, PngImagePlugin

TAG_ORIENTATION = 0x0112
TAG_MAKE, TAG_MODEL, TAG_DATETIME = 0x010F, 0x0110, 0x0132
TAG_GPS_IFD = 0x8825

XMP = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
       b'<rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:creator>SECRET-XMP-AUTHOR</dc:creator>'
       b'</rdf:Description></rdf:RDF></x:xmpmeta>')

failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print("FAIL:", what)


def make_exif(orientation):
    exif = Image.Exif()
    exif[TAG_ORIENTATION] = orientation
    exif[TAG_MAKE] = "SECRET-MAKE"
    exif[TAG_MODEL] = "SECRET-MODEL"
    exif[TAG_DATETIME] = "2024:05:01 12:34:56"
    gps = exif.get_ifd(TAG_GPS_IFD)
    gps[1] = "N"
    gps[2] = (55.0, 45.0, 21.0)
    gps[3] = "E"
    gps[4] = (37.0, 37.0, 4.0)
    return exif


def test_image(width=96, height=64):
    # Non-square, non-symmetric gradient so a wrong rotation would be visible.
    im = Image.new("RGB", (width, height))
    px = im.load()
    for y in range(height):
        for x in range(width):
            px[x, y] = ((x * 255) // width, (y * 255) // height, ((x + 2 * y) * 7) % 256)
    return im


def srgb_icc():
    return ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()


def run_strip(tool, src, dst, *flags):
    p = subprocess.run([tool, *flags, str(src), str(dst)], capture_output=True, text=True)
    return p.returncode, p.stderr


def pixels(path):
    with Image.open(path) as im:
        im.load()
        return im.mode, im.size, im.tobytes()


def displayed(path):
    with Image.open(path) as im:
        t = ImageOps.exif_transpose(im)
        return t.size, t.tobytes()


def jpeg_case(tool, work, name, orientation, progressive=False, icc=True):
    src = work / f"{name}.jpg"
    dst = work / f"{name}.stripped.jpg"
    kwargs = dict(quality=90, exif=make_exif(orientation).tobytes(), comment=b"SECRET-COMMENT", xmp=XMP)
    if progressive:
        kwargs["progressive"] = True
    if icc:
        kwargs["icc_profile"] = srgb_icc()
    test_image().save(src, "JPEG", **kwargs)

    raw = src.read_bytes()
    for marker in (b"SECRET-COMMENT", b"SECRET-XMP-AUTHOR", b"SECRET-MAKE"):
        check(marker in raw, f"{name}: precondition, input contains {marker!r}")
    with Image.open(src) as im:
        check(im.getexif().get(TAG_ORIENTATION) == orientation, f"{name}: precondition orientation")
        check(TAG_GPS_IFD in im.getexif(), f"{name}: precondition GPS present")

    rc, report = run_strip(tool, src, dst)
    check(rc == 0, f"{name}: mc1h_strip exit {rc}: {report}")
    if rc != 0:
        return
    out = dst.read_bytes()
    check(pixels(src) == pixels(dst), f"{name}: decoded pixels identical")
    check(displayed(src) == displayed(dst), f"{name}: displayed (exif_transpose) image identical")
    for marker in (b"SECRET-COMMENT", b"SECRET-XMP-AUTHOR", b"SECRET-MAKE", b"SECRET-MODEL", b"2024:05:01",
                   b"http://ns.adobe.com/xap"):
        check(marker not in out, f"{name}: output must not contain {marker!r}")
    with Image.open(dst) as im:
        exif = im.getexif()
        if orientation == 1:
            check(len(exif) == 0, f"{name}: no EXIF left for orientation 1 (got {dict(exif)})")
        else:
            check(dict(exif) == {TAG_ORIENTATION: orientation}, f"{name}: EXIF is only orientation (got {dict(exif)})")
        check(TAG_GPS_IFD not in exif and not exif.get_ifd(TAG_GPS_IFD), f"{name}: GPS removed")
        check("comment" not in im.info, f"{name}: comment removed")
        check("xmp" not in im.info, f"{name}: XMP removed")
        if icc:
            check(im.info.get("icc_profile") == srgb_icc(), f"{name}: ICC profile kept byte-identical")
    print(f"ok  {name}: {len(raw)} -> {len(out)} bytes")


def png_case(tool, work, name, orientation, mode="RGB"):
    src = work / f"{name}.png"
    dst = work / f"{name}.stripped.png"
    info = PngImagePlugin.PngInfo()
    info.add_text("Author", "SECRET-PNG-AUTHOR")
    info.add_text("Comment", "SECRET-PNG-ZTXT", zip=True)
    info.add_itxt("Description", "SECRET-PNG-ITXT", lang="en", tkey="Description")
    im = test_image()
    if mode == "RGBA":
        im = im.convert("RGBA")
        im.putalpha(Image.linear_gradient("L").resize(im.size))
    im.save(src, "PNG", pnginfo=info, exif=make_exif(orientation).tobytes(), icc_profile=srgb_icc(), dpi=(300, 300))

    raw = src.read_bytes()
    for marker in (b"SECRET-PNG-AUTHOR", b"SECRET-PNG-ITXT", b"eXIf", b"zTXt"):
        check(marker in raw, f"{name}: precondition, input contains {marker!r}")

    rc, report = run_strip(tool, src, dst)
    check(rc == 0, f"{name}: mc1h_strip exit {rc}: {report}")
    if rc != 0:
        return
    out = dst.read_bytes()
    check(pixels(src) == pixels(dst), f"{name}: decoded pixels identical")
    check(displayed(src) == displayed(dst), f"{name}: displayed (exif_transpose) image identical")
    for marker in (b"SECRET-PNG-AUTHOR", b"SECRET-PNG-ITXT", b"SECRET-MAKE", b"tEXt", b"iTXt", b"zTXt"):
        check(marker not in out, f"{name}: output must not contain {marker!r}")
    with Image.open(dst) as im:
        im.load()
        exif = im.getexif()
        check(dict(exif) == {TAG_ORIENTATION: orientation}, f"{name}: EXIF is only orientation (got {dict(exif)})")
        check(not getattr(im, "text", {}), f"{name}: no PNG text chunks (got {getattr(im, 'text', {})})")
        check(im.info.get("icc_profile") == srgb_icc(), f"{name}: ICC profile kept")
        dpi = im.info.get("dpi")
        check(dpi is not None and round(dpi[0]) == 300, f"{name}: pHYs (dpi) kept")
    print(f"ok  {name}: {len(raw)} -> {len(out)} bytes")


def options_case(tool, work):
    src = work / "opts.jpg"
    test_image().save(src, "JPEG", exif=make_exif(6).tobytes(), icc_profile=srgb_icc())
    dst = work / "opts.stripped.jpg"
    rc, report = run_strip(tool, src, dst, "--drop-orientation", "--drop-icc")
    check(rc == 0, f"opts: exit {rc}: {report}")
    with Image.open(dst) as im:
        check(len(im.getexif()) == 0, "opts: --drop-orientation leaves no EXIF")
        check("icc_profile" not in im.info, "opts: --drop-icc removes ICC")
    check(pixels(src) == pixels(dst), "opts: pixels identical")
    print("ok  options")


def rejects_case(tool, work):
    gif = work / "x.gif"
    test_image().convert("P").save(gif, "GIF")
    rc, _ = run_strip(tool, gif, work / "x.out")
    check(rc == 1 and not (work / "x.out").exists(), "unsupported format rejected without output")
    broken = work / "broken.jpg"
    good = io.BytesIO()
    test_image().save(good, "JPEG")
    broken.write_bytes(good.getvalue()[: len(good.getvalue()) // 2])
    rc, _ = run_strip(tool, broken, work / "broken.out")
    check(rc == 1 and not (work / "broken.out").exists(), "truncated JPEG rejected without output")
    rc = subprocess.run([tool, str(gif)], capture_output=True).returncode
    check(rc == 2, "usage error exit code 2")
    print("ok  rejects")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    tool, work = sys.argv[1], Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    for o in (1, 3, 6, 8):
        jpeg_case(tool, work, f"jpeg_o{o}", o)
    jpeg_case(tool, work, "jpeg_progressive_o6", 6, progressive=True)
    jpeg_case(tool, work, "jpeg_noicc_o6", 6, icc=False)
    png_case(tool, work, "png_rgb_o6", 6)
    png_case(tool, work, "png_rgba_o8", 8, mode="RGBA")
    options_case(tool, work)
    rejects_case(tool, work)
    print(f"{checks} checks, {len(failures)} failures (Pillow {Image.__version__})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
