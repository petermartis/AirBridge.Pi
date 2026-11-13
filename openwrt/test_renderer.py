#!/usr/bin/env python3
"""Hardware-free smoke and layout tests; output stays in a temporary directory."""
import pathlib
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    status = root / "status"
    status.write_text(
        "SSID\tAirBridge\nIP\t192.168.1.185\nETH\tUP\nAP\tUP\n"
        "MODE\tBRIDGED\nRADIO\tCH 37 / 6135 MHZ / 80 MHZ\nCOUNT\t1\n"
        "CLIENT\t12:34:56:78:9a:bc -48 DBM\n"
    )
    for name, options in [
        ("dashboard", ["--status", str(status)]),
        ("test", ["--test"]),
        ("missing", ["--status", str(root / "absent")]),
    ]:
        output = root / f"{name}.ppm"
        subprocess.run([binary, "--rotation", "0", *options, "--render", str(output)], check=True)
        data = output.read_bytes()
        header = b"P6\n320 240\n255\n"
        assert data.startswith(header)
        pixels = data[len(header):]
        assert len(pixels) == 320 * 240 * 3
        assert len({pixels[i:i + 3] for i in range(0, len(pixels), 3)}) > 2
        if name == "test":
            for x, expected in [(20, (255, 0, 0)), (70, (0, 255, 0)), (120, (0, 0, 255))]:
                offset = (100 * 320 + x) * 3
                assert tuple(pixels[offset:offset + 3]) == expected
    status.write_text("SSID\t" + "A" * 2000 + "\nCOUNT\t999\n" + "CLIENT\tX\n" * 40)
    subprocess.run([binary, "--status", str(status), "--render", str(root / "long.ppm")], check=True)
    assert subprocess.run([binary, "--invalid"], capture_output=True).returncode == 2
    for value in ["0", "-1", "99999999999", "1000000x", ""]:
        assert subprocess.run([binary, "--speed", value], capture_output=True).returncode == 2
    for angle, dimensions in [("0", "320 240"), ("90", "240 320"),
                              ("180", "320 240"), ("270", "240 320")]:
        output = root / f"rotation-{angle}.ppm"
        subprocess.run([binary, "--rotation", angle, "--render", str(output)], check=True)
        assert output.read_bytes().startswith(f"P6\n{dimensions}\n255\n".encode())
    assert subprocess.run([binary, "--rotation", "45"], capture_output=True).returncode == 2
    output = root / "wide.ppm"
    subprocess.run([binary, "--rotation", "90", "--geometry", "320x240",
                    "--render", str(output)], check=True)
    assert output.read_bytes().startswith(b"P6\n320 240\n255\n")
    assert subprocess.run([binary, "--geometry", "bad"], capture_output=True).returncode == 2
print("Renderer tests passed: dashboard, colors, absent data, oversized input, CLI")
